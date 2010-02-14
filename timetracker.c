#include <curses.h>
#include <stdio.h>
#include <time.h>

/*************************************************************************
 * macros
 ************************************************************************/
#define TT_NAME_SZ 80
#define MAX_TIMETRACKERS 20

/*************************************************************************
 * types
 ************************************************************************/
struct timetracker {
	/** name of the timer. */
	char name[TT_NAME_SZ];

	/** nonzero if the timer is running. */
	int running;

	/** When running = 0, this is the amount of time remaining. */
	time_t remaining_seconds;

	/** When running = 1, this is the time when the timer will be
	 * finished. */
	time_t finish_time;
};

/*************************************************************************
 * functions
 ************************************************************************/
/** Parse a line in a timetracker file
 *
 * @param line		The line to parse
 * @param timetracker	(out param) the timetracker
 *
 * @return		negative values on error,
 *			0 if no timetrackers were created,
 *			the number of timetrackers created otherwise.
 */
static int parse_timetracker(const char *line,
			     struct timetracker *timetracker)
{
	int seconds = 0;
	if (line[0] == '#')
		return 0;
	if (sscanf(line, "%" TO_STR(TT_NAME_SZ) "s=%dM",
			timetracker->name, seconds) != 2) {
		return -1000;
	}
	timetracker->running = 0;
	timetracker->remaining_seconds = seconds;
	timetracker->finish_time = 0;
	return 1;
}

/** Parse the config options, which are provided as environment variables.
 *
 * @param filename	The name of the configuration file to use
 *
 * @return		A pointer to an array of timetrackers. The last
 *			tracker will have name[0] set to the empty string.
 *			NULL will be returned on error.
 */
static struct timetracker *get_timetrackers(const char *filename)
{
	FILE *f;
	struct timetracker *ret;
	int num_tt, line_no;
	time_t now;

	f = fopen(filename, "r");
	if (!f) {
		int err = errno;
		fprintf(stderr, "%s: failed to open %s: %s (%d)\n",
			__func__, filename, strerror(err), err);
		goto cleanup0;
	}

	num_tt = 0;
	line_no = 0;
	ret = calloc(1, sizeof(struct timetracker) * (MAX_TIMETRACKERS + 1));
	if (! ret) {
		fprintf(stderr, "%s: out of memory\n", __func__);
		goto cleanup1;
	}
	while (1) {
		char line[160];
		const char *res = fgets(line, sizeof(line), f);
		line_no++;
		if (! res) {
			int err;
			if (feof(f))
				break;
			err = errno;
			fprintf(stderr, "%s: fgets error on line %d: "
				"%s (%d)\n",
				__func__, line_no, strerror(err), err);
			goto cleanup;
		}

		if (num_tt >= MAX_TIMETRACKERS) {
			fprintf(stderr, "%s: off-by-one error.\n",
				__func__);
			goto cleanup2;
		}
		res = parse_timetracker(line, ret + num_tt);
		if (res < 0) {
			fprintf(stderr, "%s: failed to parse line %d\n",
				__func__, line_no);
			goto cleanup2;
		}
		else if (res > 0) {
			num_tt += res;
		}
	}
	if (fclose(f)) {
		int err = errno;
		fprintf(stderr, "%s: fclose returned error %s (%d)\n",
			__func__, strerror(err), err);
		goto cleanup2;
	}
	if (num_tt == 0) {
		fprintf(stderr, "%s: no timetrackers found.\n",
			__func__);
		goto cleanup1;
	}
	// explicitly zero the name of the last entry.
	ret[num_tt].name[0] = '\0';
	return ret;

cleanup2:
	fclose(f);
cleanup1:
	free(ret);
cleanup0:
	return NULL;
}

/** Wrapper around mvaddstr that provides printf formatting semantics.
 *
 * @param win		the curses window to draw to
 * @param y		y-coordinate to print the string at
 * @param x		x-coordinate to print the string at
 * @param fmt		printf-style formatting string
 * @param ...		Format arguments
 */
void mvaddstr_printf(WINDOW *win, int y, int x, const char **fmt, ...)
{
	char str[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);
	mvwaddstr(y, x, str);
}

/** Draws all timetrackers on the screen.
 *
 * @param win		the curses window to draw to
 * @param timetracker	the array of timetrackers
 */
static void draw_timetrackers(WINDOW *win, struct timetracker *timetracker)
{
	int idx = 1;
	int line_no = 2;
	time_t cur_time = time(NULL);

	clear();
	for (, timetracker->name[0], timetracker++) {
		time_t rem;
		if (! timetracker->running) {
			rem = timetracker->remaining_seconds;
		}
		else if (finish_time < cur_time) {
			timetracker->remaining_seconds = 0;
			timetracker->running = 0;
			rem = 0;
		}
		else {
			rem = finish_time - cur_time;
		}
		min = rem / 60;
		sec = rem - (min * 60);
		mvaddstr_printf(win, line_no, 3, "%d:%02d       %s",
				min, sec, timetracker->name);
	}
}

static void shutdown_curses(void)
{
	clear();
	endwin();
}

/** Initialize curses
 *
 * @return		A pointer to stdscr on success; NULL
 * 			otherwise
 */
static WINDOW *init_curses(void)
{
	WINDOW *ret;
	if (atexit(shutdown_curses)) {
		fprintf(stderr, "%s: registering shutdown_curses failed.\n",
			__func__);
		return NULL;
	}

	ret = initscr();
	if (! ret)
		return NULL;
	noecho();
	intrflush(stdscr, FALSE);
	halfdelay(100);
	timeout(1);
	keypad(stdscr, TRUE);
	clear();

	return ret;
}

static void usage(void)
{
	printf("timetracker: a program to track time.\n\
This program maintains multiple stopwatches to track time.\n\
The timers are defined in a configuration file.\n\
\n\
usage: timetracker [options]\n\
options include:\n\
-f [conf file]           The configuration file to use\n\
-h                       Print this help message and quit\n\
");
}

int main(int argc, char **argv)
{
	int c;
	const char *conf_file = NULL;
	WINDOW *win;
	struct timetracker *timetrackers;
        chtype k;

	while ((c = getopt(argc, argv, "f:h")) != -1) {
		switch(c) {
		case 'f':
			conf_file = optarg;
			break;

		case 'h':
			usage();
			exit(1);
			break;
		case ':':
			fprintf(stderr,
				"Option -%c requires an operand\n", optopt);
			usage();
			exit(1);
			break;
		case '?':
			fprintf(stderr, "Unrecognized option: -%c\n", optopt);
			usage();
			exit(1);
			break;
		}
	}
	if (!conf_file) {
		fprintf(stderr, "You must specify a configuration file.\n");
		usage();
		exit(1);
	}

	// parse config options
	timetrackers = get_timetrackers(conf_file);
	if (! timetrackers) {
		fprintf(stderr, "error initializing timetrackers.\n");
		exit(1);
	}

	win = init_curses();
	if (! win) {
		fprintf(stderr, "error initializing the curses library.\n");
		exit(1);
	}

	while (1) {
		draw_timetrackers(win, timetrackers);
		k = getch();
		if (k == 'q')
			exit(0);
	}
	return 0;
}
