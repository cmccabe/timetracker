#include <curses.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*************************************************************************
 * macros
 ************************************************************************/
#define TT_NAME_SZ 80
#define MAX_TIMETRACKERS 9
#define TO_STR_HELPER(x) #x
#define TO_STR(x) TO_STR_HELPER(x)

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

/** Represents a timetracker color */
struct tt_color {
	int fg, bg;
};

/** Names a timetracker color */
enum tt_color_name {
	TT_COLOR_NORMAL = 1,
	TT_COLOR_RUNNING = 2,
	TT_COLOR_DONE = 3,
	TT_COLOR_MAX,
};

/*************************************************************************
 * globals
 ************************************************************************/
/** Nonzero if we are drawing colors */
int g_use_color;

const static struct tt_color tt_colors[TT_COLOR_MAX] = {
	[ TT_COLOR_NORMAL ] = { COLOR_BLACK, COLOR_WHITE },
	[ TT_COLOR_RUNNING ] = { COLOR_GREEN, COLOR_WHITE },
};

/*************************************************************************
 * functions
 ************************************************************************/
/** Turns off a timetracker
 *
 * @param timetracker		the timetracker
 *
 * @return			0 on success; error code otherwise
 */
static int timetracker_off(struct timetracker *timetracker)
{
	time_t cur_time;
	if (! timetracker->running)
		return 0;
	cur_time = time(NULL);
	if (timetracker->finish_time <= cur_time) {
		timetracker->remaining_seconds = 0;
		timetracker->running = 0;
		timetracker->finish_time = 0;
		return 0;
	}
	timetracker->remaining_seconds = timetracker->finish_time - cur_time;
	timetracker->running = 0;
	timetracker->finish_time = 0;
	return 0;
}

/** Turns on a timetracker
 *
 * @param timetracker		the timetracker
 *
 * @return			0 on success; error code otherwise
 */
static int timetracker_on(struct timetracker *timetracker)
{
	time_t cur_time;
	if (timetracker->running)
		return 0;
	cur_time = time(NULL);
	timetracker->finish_time = cur_time + timetracker->remaining_seconds;
	timetracker->running = 1;
	timetracker->remaining_seconds = 0;
	return 0;
}

/** Parse a line in a timetracker file
 *
 * @param line		The line to parse
 * @param timetracker	(out param) the timetracker
 *
 * @return		negative values on error,
 *			0 if no timetrackers were created,
 *			the number of timetrackers created otherwise.
 */
static int parse_timetracker(char *line,
			     struct timetracker *timetracker)
{
	int len, res;
	int minutes;
	// reject comments
	if (line[0] == '#')
		return 0;

	// reject empty lines
        len = strlen(line);
        if (len < 1)
		return 0;

	// trim newline if present
        if (line[len - 1] == '\n') {
		line[len - 1] = '\0';
		len--;
	}
	res = sscanf(line, "%" TO_STR(TT_NAME_SZ) "[^=]=%dM",
			timetracker->name, &minutes);
	if (res != 2) {
		return -1000;
	}
	timetracker->running = 0;
	timetracker->remaining_seconds = minutes * 60;
	timetracker->finish_time = 0;
	return 1;
}

/** Parse the config options, which are provided as environment variables.
 *
 * @param filename	The name of the configuration file to use
 * @param timetrackers	(out param) A handle to an array of timetrackers
 * @param num_trackers	(out param) The number of timetrackers in the array
 *
 * @return		0 on success; error code otherwise
 */
static int get_timetrackers(const char *filename,
	struct timetracker **timetrackers, int *num_trackers)
{
	FILE *f;
	struct timetracker *ret;
	int num_tt, line_no;

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
                int out;
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
			goto cleanup2;
		}

		if (num_tt >= MAX_TIMETRACKERS) {
			fprintf(stderr, "%s: off-by-one error.\n",
				__func__);
			goto cleanup2;
		}
		out = parse_timetracker(line, ret + num_tt);
		if (out < 0) {
			fprintf(stderr, "%s: failed to parse line %d (%s)\n",
				__func__, line_no, line);
			goto cleanup2;
		}
		else if (out > 0) {
			num_tt += out;
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
	*num_trackers = num_tt;
	*timetrackers = ret;
	return 0;

cleanup2:
	fclose(f);
cleanup1:
	free(ret);
cleanup0:
	return 1;
}

/** Wrapper around mvaddstr that provides printf formatting semantics.
 *
 * @param win		the curses window to draw to
 * @param y		y-coordinate to print the string at
 * @param x		x-coordinate to print the string at
 * @param fmt		printf-style formatting string
 * @param ...		Format arguments
 */
void mvaddstr_printf(WINDOW *win, int y, int x, const char *fmt, ...)
{
	char str[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);
	mvwaddstr(win, y, x, str);
}

/** Draws all timetrackers on the screen.
 *
 * @param win		the curses window to draw to
 * @param timetracker	the array of timetrackers
 */
static void draw_timetrackers(WINDOW *win, struct timetracker *timetracker)
{
	int item_no = 1, line_no = 2;
	time_t cur_time = time(NULL);

	clear();
	for (; timetracker->name[0]; timetracker++) {
		time_t rem;
                int min, sec;
		int tt_color = COLOR_PAIR(TT_COLOR_NORMAL);
		if (!timetracker->running) {
			rem = timetracker->remaining_seconds;
		}
		else if (timetracker->finish_time < cur_time) {
			timetracker_off(timetracker);
			rem = 0;
		}
		else {
			rem = timetracker->finish_time - cur_time;
		}
		min = rem / 60;
		sec = rem - (min * 60);
		if (g_use_color) {
			if (timetracker->running)
				tt_color = COLOR_PAIR(TT_COLOR_RUNNING);
			attron(tt_color);
		}
		mvaddstr_printf(win, line_no, 3,
			" [%d]    %3d:%02d       %s",
			item_no, min, sec, timetracker->name);
		if (g_use_color) {
			attroff(tt_color);
		}
		line_no += 2;
		item_no++;
	}
	mvwaddstr(win, 0, 0, " ");
}

static void shutdown_curses(void)
{
	clear();
	endwin();
}

/** Initialize colors in the curses library
 *
 * @return		0 on success; error code otherwise
 */
static int timetracker_init_color(void)
{
	size_t i;
	if (assume_default_colors(COLOR_BLACK, COLOR_WHITE)) {
		fprintf(stderr, "%s: your terminal does not support "
			"assume_default_colors. Avoiding color because it "
			"would probably look ugly.\n", __func__);
		return 1000;
	}
	if (start_color() == ERR) {
		fprintf(stderr, "%s: start_color failed.\n", __func__);
		return 1001;
	}
	for (i = 1; i < TT_COLOR_MAX; i++) {
		const struct tt_color *c = tt_colors + i;
		if (init_pair(i, c->fg, c->bg) == ERR) {
			fprintf(stderr, "%s: init_pair(%d) failed.\n",
				__func__, i);
		}
	}
	return 0;
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
	if (g_use_color) {
		int res = timetracker_init_color();
		if (res) {
			g_use_color = 0;
			fprintf(stderr, "%s: start_color failed with "
				"error code %d. Does this terminal "
				"support color?\n", __func__, res);
		}
	}

	noecho();
	intrflush(stdscr, FALSE);
	halfdelay(1);
	//timeout(1);
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
-N                       Do *not* try to use color\n\
");
}

int main(int argc, char **argv)
{
	int c;
	const char *conf_file = NULL;
	WINDOW *win;
	struct timetracker *timetrackers;
	int num_timetrackers;
        chtype k;
	g_use_color = 1;

	while ((c = getopt(argc, argv, "f:hN")) != -1) {
		switch(c) {
		case 'f':
			conf_file = optarg;
			break;

		case 'h':
			usage();
			exit(1);
			break;
		case 'N':
			g_use_color = 0;
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
	if (get_timetrackers(conf_file, &timetrackers, &num_timetrackers)) {
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
                if (k >= '1' && k <= '9') {
			int n = k - '1';
			if (n < num_timetrackers) {
				if (timetrackers[n].running)
					timetracker_off(timetrackers + n);
				else
					timetracker_on(timetrackers + n);
			}
                }
	}
	return 0;
}
