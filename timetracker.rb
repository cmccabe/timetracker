#!/usr/bin/ruby

require "curses"
include Curses

####################### local ####################### 
def setupcolors()
  Curses.start_color
    [Curses::COLOR_RED,
     Curses::COLOR_GREEN, 
     Curses::COLOR_YELLOW, 
     Curses::COLOR_BLUE, 
     Curses::COLOR_CYAN,
     Curses::COLOR_MAGENTA ].each do |c|
      Curses.init_pair(c, c, Curses::COLOR_WHITE)
    end

  # A workaround for a curses "quirk"
  $true_black = 7
  Curses.init_pair(Curses::COLOR_WHITE,
                   Curses::COLOR_BLACK, Curses::COLOR_WHITE)
end

$colors = Array.new

def push_color(color)
  $colors << color
  stdscr.color_set(color)
end

def pop_color()
  $colors.pop  
  oldcolor = $colors[-1]
  if (oldcolor == nil) then
    oldcolor = $true_black
  end
  stdscr.color_set(oldcolor)
end

####################### classes ####################### 
class BoxLine
  attr_reader :string 
  attr_reader :centered

  def initialize(string, centered, color)
    @string = string
    @centered = centered
    @color = color
  end

  def draw(y, x0, x1)
    if (@color != nil) then
      push_color(@color)
    end
    if (@centered) then
      slen = string.length()
      tlen = (x1 - x0) - 2
      if (slen <= tlen) then
        setpos(y, (tlen - slen) / 2)
      else 
        setpos(y, x0 + 2)
      end
    else
      setpos(y, x0 + 2)
    end

    addstr(string)
    if (@color != nil) then
      pop_color()
    end
  end
end

class Box
  attr_reader :max_lines

  @@latest_line_num = 4

  def self.get_slot
    ret = @@latest_line_num 
    @@latest_line_num = @@latest_line_num + 2
    return ret
  end

  def initialize(x0, y0, x1, y1)
    @x0 = x0
    @y0 = y0
    @x1 = x1
    @y1 = y1
    @boxlines = Hash.new()
  end

  def add_line(line_num, string, centered, color)
    @boxlines[line_num] = BoxLine.new(string, centered, color)
  end

  def clearall()
    for y in (@y0..@y1)
      setpos(y, @x0)
      addstr(" " * (@x1 - @x0))
    end
  end

  def draw()
    horiz_extent = @x1 - @x0

    setpos(@y0, @x0)
    s = String.new("#")
    s *= horiz_extent 
    addstr(s)

    for y in (@y0..@y1)
      setpos(y, @x0)
      addstr("#")
      if (@boxlines.has_key?(y)) then
        @boxlines[y].draw(y, @x0, @x1)
      end
      setpos(y, @x1)
      addstr("#")
    end

    setpos(@y1, @x0)
    t = String.new("#")
    t *= horiz_extent 
    addstr(t)
  end
end

# Subject class that sends out notifications to classes listening for
# keystrokes.
class KeystrokeSubject
  def initialize()
    @observers = Hash.new
  end

  def send_key(k)
    if (@observers.has_key?(k)) then
      #out = sprintf("send_key %d", k)
      #addstr(out)
      @observers[k].handle_keystroke(k)
    end
  end

  def add_obs(k, obs)
    if (@observers.has_key?(k)) then
      throw "tried to bind the same key twice!"
    else 
      @observers[k] = obs
    end
  end
end

# Simple keystroke observer that just throws an exception when its 
# key is pressed.
class Quitter
  def initialize(keystroke_subject, box)
    keystroke_subject.add_obs(?q, self)
    
    box.add_line(Box.get_slot(), "[q] Quit", false, nil)
  end

  def handle_keystroke(k)
    throw "quitting"
  end
end

# Keystroke observer that saves all tasks in the taskset
class Saver
  def initialize(keystroke_subject, box, task_set, task_file)
    keystroke_subject.add_obs(?s, self)
    @slot = Box.get_slot()
    @task_set = task_set
    @task_file = task_file
    @box = box
    
    @box.add_line(@slot, "[s] Save", false, nil)
  end

  def handle_keystroke(k)
    outfile = File.new(@task_file, "w")
    outfile.puts @task_set

    curtime = Time.new
    curtimestr = sprintf("%02d:%02d", curtime.hour, curtime.min)
    @box.add_line(@slot, "[s] Saved at #{curtimestr}", false, nil)
    @box.draw
  end
end

# Keystroke observer that zeros the timers of all tasks
class Zeroer
  def initialize(keystroke_subject, box, task_set)
    keystroke_subject.add_obs(?z, self)
    @slot = Box.get_slot()
    @task_set = task_set
    @box = box
    
    @box.add_line(@slot, "[z] Zero All", false, nil)
  end

  def handle_keystroke(k)
    @task_set.foreach_task() { |task| task.zero_timer() }
  end
end

# A set of tasks. Knows how to load a task file.
# Relays on handle_keystroke events to the tasks themselves.
class TaskSet
  attr_reader :description

  def initialize(keystroke_subject, filename, box)
    @keystroke_subject = keystroke_subject
    @box = box

    # Load the task set from a file
    @tasks = Hash.new
    file = File.new(filename, "r")
    counter = 0
    @description = file.gets
    @description.chomp!
    while (line = file.gets)
      line.chomp!
      counter = counter + 1
      tokens = line.split('|')
      curtime = TaskTime.CreateTaskTime(tokens[2])
      goaltime = TaskTime.CreateTaskTime(tokens[3])
      add_task(tokens[0], tokens[1], goaltime, curtime)
    end
  end

  def add_task(key, taskname, goaltime, curtime)
    task = Task.new(key, taskname, goaltime, curtime, @box)
    @tasks[key] = task
    @keystroke_subject.add_obs(key[0], task) 
  end

  def foreach_task()
    @tasks.each {|key, val| yield val}
  end

  #def handle_keystroke(k)
    #@tasks[k].handle_keystroke()
  #end

  def to_s
    ret = String.new
    ret << description << "\n"
    @tasks.each do |key, task| 
      ret << task.to_s
    end
    return ret
  end
end

# Represents a task.
class Task 
  attr_reader :key
  attr_reader :taskname
  attr_reader :goaltime
  attr_reader :curtime

  def initialize(key, taskname, goaltime, curtime, box)
    @key = key
    @taskname = taskname
    @goaltime = goaltime
    @curtime = curtime
    @cur_line_num = Box.get_slot
    @box = box
  end

  def handle_keystroke(k)
    if ($task_updater.has_task(key)) then
      $task_updater.stop_task(key)
    else
      $task_updater.add_task(key, self)
    end
  end

  def add_self_to_box()
    if (@curtime.timer_started?()) then
      color = Curses::COLOR_RED
    end

    self_text = sprintf("[%s] %s/%s    %s", 
                        key, curtime.to_s(), goaltime.to_s(), taskname)
    @box.add_line(@cur_line_num, self_text, false, color)
    @box.draw()
  end

  def timer_started?
    return @curtime.timer_started?()
  end

  def start_timer(curtime)
    @curtime.start_timer(curtime)
    add_self_to_box()
  end

  def update_timer(curtime)
    add_self_to_box()
  end

  def end_timer(curtime)
    @curtime.end_timer(curtime)
    add_self_to_box()
  end

  def zero_timer()
    curtime = Time.new
    end_timer(curtime)
    @curtime.zero()
    add_self_to_box()
  end

  def to_s
    ret = String.new
    ret << @key << "|" << @taskname << "|" 
    ret << @curtime.to_s << "|" << @goaltime.to_s << "\n"
    return ret
  end
end

# Represents a task time.
class TaskTime
  attr_reader :finish_time

  def self.CreateTaskTime(string)
    tokens = string.split(':')
    if (tokens.length != 2) then
      throw("Malformed time string: \"#{string}\".\n"\
"The time string must be in the form HH:MM, where "\
"H is hours and M is minutes")
    end
    hours = tokens[0].to_i
    minutes = tokens[1].to_i
    return TaskTime.new(hours, minutes, 0)
  end

  def initialize(hours, minutes, seconds)
    @seconds = (60 * 60 * hours) + (60 * minutes) + seconds
    @start_time = nil
  end

  def timer_started?
    return (@start_time != nil)
  end

  def start_timer(curtime)
    @start_time = curtime.to_i
  end

  def end_timer(curtime)
    @seconds = get_seconds(Time.new)  
    @start_time = nil
  end

  def zero
    @seconds = 0
    @start_time = nil
  end

  def get_seconds(curtime)
    if (@start_time != nil) then
      return (curtime.to_i - @start_time) + @seconds
    else
      return @seconds
    end
  end

  def to_s()
    curseconds = get_seconds(Time.new)
    hours = (curseconds / 3600).floor()
    sec_remainder = curseconds - (hours * 3600)
    minutes = (sec_remainder / 60).floor()
    seconds = sec_remainder - (minutes * 60)
    return sprintf("%02d:%02d", hours, minutes)
  end
end

class TaskUpdater
  $task_updater = TaskUpdater.new
  @@running_tasks = Hash.new

  def initialize
  end

  def update_all()
    curtime = Time.new
    @@running_tasks.each do |key, task|
      task.update_timer(curtime)
    end
  end

  def has_task(key)
    return (@@running_tasks.has_key?(key))
  end
  
  def add_task(key, task)
    if (@@running_tasks[key] == nil) then
      @@running_tasks[key] = task
      task.start_timer(Time.new)
    end
  end

  def stop_task(key)
    task = @@running_tasks[key]
    if (task != nil) then
      task.end_timer(Time.new)
    end
    @@running_tasks.delete(key)
  end
end

####################### main ####################### 
init_screen()
start_color()
setupcolors()
#use_default_colors()
stdscr.color_set($true_black) 
stdscr.timeout=(5)
begin
  ##### handle arguments
  task_file = ARGV[0]

  ##### some curses foo
  crmode
  raw
  noecho

  ##### main event loop 
  # TODO: handle screen refreshes and resizings.
  box = Box.new(0, 0,
              Curses::stdscr.maxx - 1, Curses::stdscr.maxy - 1)

  keystroke_subject = KeystrokeSubject.new
  quitter = Quitter.new(keystroke_subject, box)
  task_set = TaskSet.new(keystroke_subject, task_file, box)
  saver = Saver.new(keystroke_subject, box, task_set, task_file)
  zeroer = Zeroer.new(keystroke_subject, box, task_set)

  task_set.foreach_task() {|task| task.add_self_to_box()} 
  box.add_line(2, task_set.description, true, nil)

  box.clearall()
  box.draw()

  while (true)
    setpos(4, 2)
    begin
      ch = getch 
      if (ch != 4294967295) then
        keystroke_subject.send_key(ch)
      end

      $task_updater.update_all()
    end
  end
ensure
  close_screen
  # todo: print out exception backtrace
end

  #clrtoeol()   
  #TODO: rewrite in ncurses rather than curses?
  #TODO: prefix curses methods with "stdscr"?
  #TODO: support clearing the time for a given task

