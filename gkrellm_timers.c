/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/* Timer plugin for GKrellM 
|  Copyright (C) 2001  Christian W. Zuckschwerdt <zany@triq.net>
|
|  Author:  Christian W. Zuckschwerdt  <zany@triq.net>  http://triq.net/
|  Latest versions might be found at:  http://gkrellm.net/
|
| This program is free software; you can redistribute it and/or
| modify it under the terms of the GNU General Public License
| as published by the Free Software Foundation; either version 2
| of the License, or (at your option) any later version.
|
| This program is distributed in the hope that it will be useful,
| but WITHOUT ANY WARRANTY; without even the implied warranty of
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
| GNU General Public License for more details.
|
| You should have received a copy of the GNU General Public License
| along with this program; if not, write to the Free Software
| Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/* Installation:
|
|     make
|     make install
|      or without using superuser privileges
|     make install-user
|
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>

#include <gkrellm/gkrellm.h>

#include "bg_meter_alarm.xpm"

#if (VERSION_MAJOR <= 1)&&(VERSION_MINOR < 2)
/* Additional public GKrellM function prototype */
void create_config_window(void);
#define gkrellm_open_config_window(monitor) create_config_window()
#endif

#define SNMP_PLUGIN_MAJOR_VERSION 1
#define SNMP_PLUGIN_MINOR_VERSION 0

#define PLUGIN_CONFIG_KEYWORD   "timers"
#define CONFIG_NAME "Timers"     /* Name in the configuration window */
#define STYLE_NAME  "timers"    /* Theme subdirectory name and gkrellmrc */
                                /*  style name.                 */
#define YES "yes"
#define NO "no"
#define STOPWATCH "Stopwatch"
#define NOT_STOPWATCH "Timer"
#define ACTIVE "Running"
#define NOT_ACTIVE "Stopped"

/* the whole formating stuff is from clock.c */
typedef struct
	{
	TextStyle	ts;
	gint		lbearing,
			rbearing,
			width,
			ascent,
			descent;
	}
	Extents;

typedef struct Timer Timer;

struct Timer
	{
	Timer		*next;
	gint		id;
	gchar		*label;
	gboolean	stopwatch;	/* timer or stopwatch? */
	gboolean	restart;	/* restart immediately?  */
	gboolean	popup;		/* raise a popup? */
	gboolean	active;		/* running? */
	gboolean	triggered;	/* triggered and no reset so far */
        gchar		*command;
	time_t		set_time; /* user set time */
	time_t		cur_time; /* remaining time / time so far */
        gboolean	needs_refresh;
	Panel		*panel;
	Decal		*d_clock;
	Decal		*d_seconds;
	Decal		*d_alarm;
	GdkPixmap	*alarm_pixmap;
	GdkBitmap	*alarm_mask;
	GtkTooltips	*tooltip;
	};

static Extents		time_extents,
			sec_extents;


static void
string_extents(gchar *string, Extents *ext)
	{
	gdk_string_extents(ext->ts.font, string,
                           &ext->lbearing, &ext->rbearing,
                           &ext->width, &ext->ascent, &ext->descent);
	}

static void
launch_command(gchar *command)
        {
        gchar           *command_line;

        command_line = g_strconcat(command, " &", NULL);
        system(command_line);
        g_free(command_line);
        }

static struct tm *
my_localtime(const time_t *timep)
    {
    static struct tm    t_local;
    struct tm           *t;
    time_t              t_now;

    time(&t_now);

    /* Call localtime() and copy its buffer to my local static buffer.
    */
    if (timep == NULL)
        t = localtime(&t_now);
    else
        t = localtime(timep);
    t_local = *t;

    /* Restore the localtime() buffer to values expected by
    |  the clock monitor.
    */
    localtime(&t_now);

    return &t_local;
    }


static void
reset_timer(Timer *timer)
{
    struct tm *t;

    if (timer->stopwatch)
    {
        t = my_localtime(NULL);
        t->tm_hour = 0;
        t->tm_min = 0;
        t->tm_sec = 0;

        timer->cur_time = mktime( t );
    }
    else
    {
        timer->cur_time = timer->set_time;
    }
    timer->needs_refresh = TRUE;
}

static void
timer_popup(Timer *timer)
{
    gchar timebuf[32];
    gchar *text;
    struct tm *t;

    t = my_localtime(&timer->set_time);

    strftime(timebuf, sizeof(timebuf), "%k:%M:%S", t);

    text = g_strdup_printf("The %s %s set to %s just went off!",
                           timer->stopwatch ? STOPWATCH : NOT_STOPWATCH,
                           timer->label, timebuf);

    gkrellm_message_window("Alarm!", text,
                           timer->panel->drawing_area);
    g_free(text);
}


static void
timer_alarm(Timer *timer)
{
    if (timer == NULL)
        return;

    reset_timer(timer);
    if (!timer->restart)
        timer->active = FALSE;

    if (timer->popup)
        timer_popup(timer);

    if ( (timer->command == NULL) || (timer->command[0] == '\0'))
        return;
    launch_command(timer->command);
}

/*
static Timer *
new_timer(gint hours, gint mins, gint secs)
{
    Timer *timer;
    struct tm *t;

    t = my_localtime(NULL);
    t->tm_hour = hours;
    t->tm_min = mins;
    t->tm_sec = secs;

    timer = g_new0(Timer, 1); 
    timer->set_time = mktime( t );
    reset_timer(timer);
    gkrellm_dup_string(&timer->label, "");

    return timer;
}
*/

static void
set_tooltip(Timer *timer)
{
    gchar timebuf[32];
    gchar *text;
    struct tm *t;

    t = my_localtime(&timer->set_time);

    strftime(timebuf, sizeof(timebuf), "%k:%M:%S", t);

    text = g_strdup_printf("%s: %s %s is set to %s",
                           timer->active ? ACTIVE : NOT_ACTIVE,
                           timer->stopwatch ? STOPWATCH : NOT_STOPWATCH,
                           timer->label, timebuf);

    gtk_tooltips_set_tip(timer->tooltip, timer->panel->drawing_area, text, "");
    gtk_tooltips_enable(timer->tooltip);
    g_free(text);
/*
	    gtk_tooltips_disable(timer->tooltip);
*/      
}

static Timer      *timers;
static GtkWidget  *main_vbox;
static gint       style_id;
static Monitor    *plugin_mon_ref;

static Timer *
find_timer(GtkWidget *widget)
{
    Timer *timer;

    for (timer = timers; timer ; timer = timer->next)
        if (timer->panel->drawing_area == widget)
            return timer;
    return NULL;
}


static gint
cb_panel_press(GtkWidget *widget, GdkEventButton *ev)
{
    Timer *timer;

    /* Button 1 toggles the active switch
     */
    if (ev->button == 1)
    {
        timer = find_timer(widget);
        if (timer)
        {
            if (timer->triggered)
            {
                timer->triggered = FALSE;
                gkrellm_make_decal_invisible(timer->panel, timer->d_alarm);
            }
            else
            {
                timer->active = !timer->active;
                set_tooltip(timer);
            }
        }

    }
    /* Button 2 resets the timer
     */
    if (ev->button == 2)
    {
        timer = find_timer(widget);
        if (timer)
        {
            reset_timer(timer);
            timer->triggered = FALSE;
            gkrellm_make_decal_invisible(timer->panel, timer->d_alarm);
        }
    }
    /* Button 3 calls up the configuration dialog
     */
    if (ev->button == 3)
    {
        gkrellm_open_config_window( plugin_mon_ref );
    }
    return TRUE;
}       


static void
update_plugin()
{
    Timer *timer;

    gchar hm[32], sec[32];
    gint w;
    struct tm *t;

    for (timer = timers; timer ; timer = timer->next)
    {

        if ( timer->needs_refresh || GK.second_tick )
//(GK.timer_ticks % TICKS_PER_S) == 0 )
        {
//FIXME:
            if (timer->active && GK.second_tick)
                timer->stopwatch ? timer->cur_time++ : timer->cur_time--;
            t = my_localtime(&timer->cur_time);

	    if (t->tm_hour < 10)
		strftime(hm, sizeof(hm), "-%k:%M", t); /* - 24 hour:minute */
	    else
		strftime(hm, sizeof(hm), "%k:%M", t); /* 24 hour:minute */
            w = gdk_string_width(timer->d_clock->text_style.font, hm);
            timer->d_clock->x_off = (w < timer->d_clock->w) ? (timer->d_clock->w - w) / 2 : 0;
            gkrellm_draw_decal_text(timer->panel, timer->d_clock, hm, t->tm_min);

            strftime(sec, sizeof(sec), "%S", t); /* seconds 00-59 */
            gkrellm_draw_decal_text(timer->panel,
                                    timer->d_seconds, sec, t->tm_sec);
            gkrellm_draw_decal_pixmap(timer->panel, timer->d_alarm, 0);

            gkrellm_draw_layers(timer->panel);
            timer->needs_refresh = FALSE;

            if (!timer->stopwatch &&
                (t->tm_hour == 0) && (t->tm_min == 0) && (t->tm_sec == 0) )
            {
                timer->triggered = TRUE;
                set_tooltip(timer);
                timer_alarm(timer);
            }

            if (timer->triggered)
            {
                if (gkrellm_is_decal_visible(timer->d_alarm) )
                    gkrellm_make_decal_invisible(timer->panel, timer->d_alarm);
                else
                    gkrellm_make_decal_visible(timer->panel, timer->d_alarm);
            }
        }
        if ( GK.minute_tick )
	    set_tooltip(timer);
    }

}

static gint
panel_expose_event(GtkWidget *widget, GdkEventExpose *ev)
{
    Timer *timer;

    for (timer = timers; timer ; timer = timer->next)
        if (widget == timer->panel->drawing_area)
            gdk_draw_pixmap(widget->window,
                            widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
                            timer->panel->pixmap,
                            ev->area.x, ev->area.y,
                            ev->area.x, ev->area.y,
                            ev->area.width, ev->area.height);
    return FALSE;
}

static void
create_timer(GtkWidget *vbox, Timer *timer, gint first_create)
{
    static gint            clock_style_id, local_style_id;
    Style                  *style;
    static GdkImlibImage   *bg_meter_alarm_image;

    if (first_create)
    {
        timer->panel = gkrellm_panel_new0();
    }
    else
    {
        gkrellm_destroy_decal_list(timer->panel);
        gkrellm_destroy_krell_list(timer->panel);
    }

    clock_style_id = gkrellm_lookup_meter_style_id(CLOCK_STYLE_NAME);
    style = gkrellm_meter_style(style_id);

    if (style == gkrellm_meter_style(DEFAULT_STYLE_ID))
    {
        local_style_id = clock_style_id;
        style = gkrellm_meter_style(clock_style_id);
    }
    local_style_id = clock_style_id; // Anyway... FIXME

    /* create the text decal first so we have the right dimensions
     */
    time_extents.ts = *gkrellm_meter_textstyle(local_style_id);
    string_extents("00:00", &time_extents);
    sec_extents.ts = *gkrellm_meter_alt_textstyle(local_style_id);
    string_extents("8M", &sec_extents);

    timer->d_clock = gkrellm_create_decal_text(timer->panel, "88",
                                               &time_extents.ts,
                                               style, 0, -1 /*top margin*/,
                                               time_extents.width + 2);
    timer->d_seconds = gkrellm_create_decal_text(timer->panel, "88",
                                                 &sec_extents.ts,
                                                 style, 0, -1,
                                                 sec_extents.width + 2);

    timer->d_clock->x = (gkrellm_chart_width() - timer->d_clock->w - timer->d_seconds->w) / 2;
    timer->d_seconds->x = timer->d_clock->x + timer->d_clock->w + 2;
    timer->d_seconds->y = timer->d_clock->y + timer->d_clock->h - timer->d_seconds->h;

    /* create the pixmap decal, remove and prepend it
     */
    gkrellm_load_image("bg_meter_alarm", bg_meter_alarm_xpm,
                       &bg_meter_alarm_image, STYLE_NAME);

    gkrellm_render_to_pixmap(bg_meter_alarm_image,
                             &timer->alarm_pixmap, &timer->alarm_mask,
                             gkrellm_chart_width(), timer->d_clock->h);

    timer->d_alarm = gkrellm_create_decal_pixmap(timer->panel,
                                                 timer->alarm_pixmap,
                                                 timer->alarm_mask,
                                                 1, /* depth*/
                                                 style, 0, -1 /*top margin*/);

    timer->panel->decal = g_list_remove(timer->panel->decal, timer->d_alarm);
    timer->panel->decal = g_list_prepend(timer->panel->decal, timer->d_alarm);

    /* Configure panel calculates the panel height needed for the decals.
    */
    gkrellm_configure_panel(timer->panel, NULL, style);

    /* Build the configured panel with a background image and pack it into
    |  the vbox assigned to this monitor.
    */
    gkrellm_create_panel(vbox, timer->panel, 
                         gkrellm_bg_meter_image(local_style_id));
    gkrellm_monitor_height_adjust(timer->panel->h);

    if (first_create) {
        gtk_signal_connect(GTK_OBJECT (timer->panel->drawing_area),
			   "expose_event",
			   (GtkSignalFunc) panel_expose_event, NULL);
        gtk_signal_connect(GTK_OBJECT (timer->panel->drawing_area),
                           "button_press_event",
                           (GtkSignalFunc) cb_panel_press, NULL);

	timer->tooltip = gtk_tooltips_new();
    }
    gkrellm_make_decal_invisible(timer->panel, timer->d_alarm);
    set_tooltip(timer);
}

static void
destroy_timer(Timer *timer)
{
    if (!timer)
        return;

    g_free(timer->label);
    g_free(timer->command);
    gdk_imlib_free_pixmap(timer->alarm_pixmap);

    gkrellm_monitor_height_adjust( - timer->panel->h);
    gkrellm_destroy_panel(timer->panel);
    //  gtk_widget_destroy(timer->vbox);
    g_free(timer);
}

static void
create_plugin(GtkWidget *vbox, gint first_create)
{
    Timer *timer;

    main_vbox = vbox;

    for (timer = timers; timer ; timer = timer->next) {
        create_timer(vbox, timer, first_create);
    }
}

/* Config section */

static GtkWidget        *label_entry;
static GtkObject        *hours_spin_adj;
static GtkWidget        *hours_spin;
static GtkObject        *mins_spin_adj;
static GtkWidget        *mins_spin;
static GtkObject        *secs_spin_adj;
static GtkWidget        *secs_spin;
static GtkWidget        *timer_radio;
static GtkWidget        *stopwatch_radio;
static GtkWidget        *restart_toggle;
static GtkWidget        *popup_toggle;
static GtkWidget        *command_entry;
static GtkWidget        *start_button;
static GtkWidget        *stop_button;
static GtkWidget        *reset_button;
static GtkWidget        *timer_clist;
static gint             selected_row = -1;
static gint             list_modified;
static gint             selected_id = -1;
#define CLIST_WIDTH 9

#define	 STR_DELIMITERS	" \t"

static void
save_plugin_config(FILE *f)
{
    Timer *timer;
    gchar *label;

    for (timer = timers; timer ; timer = timer->next) {
        label = g_strdelimit(g_strdup(timer->label), STR_DELIMITERS, '_');
        if (label[0] == '\0') label = strdup("_");

        fprintf(f, "%s %ld %d %d %d %s %s\n",
                PLUGIN_CONFIG_KEYWORD,
                timer->set_time,
                timer->stopwatch,
                timer->restart,
                timer->popup,
                label, timer->command ? timer->command : "");
        g_free(label);
    }
}

static gint next_id = 1;

static void
load_plugin_config(gchar *arg)
{
    Timer *timer, *ntimer;
    gchar label[CFG_BUFSIZE];
    gchar command[CFG_BUFSIZE];
    gint  n;

    timer = g_new0(Timer, 1); 

    n = sscanf(arg, "%ld %d %d %d %s %[^\n]",
               &timer->set_time,
               &timer->stopwatch,
               &timer->restart,
               &timer->popup,
               label, command);

    timer->id = next_id++;
    gkrellm_dup_string(&timer->label, label);
    g_strdelimit(timer->label, "_", ' ');

    if (n >= 5)
        gkrellm_dup_string(&timer->command, command);
    else
        gkrellm_dup_string(&timer->command, "");

    reset_timer(timer);

    if (!timers)
        timers = timer;
    else { 
        for (ntimer = timers; ntimer->next ; ntimer = ntimer->next);
        ntimer->next = timer;
    }

}

static void
apply_plugin_config()
{
    Timer *timer, *ntimer, *otimers;
    gchar  *name;
    gint   row;
    struct tm *t;

    if (!list_modified)
        return;

    t = my_localtime(NULL);
    otimers = timers;
    timers = NULL;

    for (row = 0; row < GTK_CLIST(timer_clist)->rows; ++row)
    {
      gint i;
      i = 0;
      ntimer = g_new0(Timer, 1);

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      ntimer->id = atoi(name);

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      gkrellm_dup_string(&ntimer->label, name);

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      t->tm_hour = atoi(name);
      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      t->tm_min = atoi(name);
      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      t->tm_sec = atoi(name);
      ntimer->set_time = mktime( t );
      reset_timer(ntimer);

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      ntimer->stopwatch = (strcmp(name, STOPWATCH) == 0) ? TRUE : FALSE;

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      ntimer->restart = (strcmp(name, YES) == 0) ? TRUE : FALSE;

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      ntimer->popup = (strcmp(name, YES) == 0) ? TRUE : FALSE;

      gtk_clist_get_text(GTK_CLIST(timer_clist), row, i++, &name);
      gkrellm_dup_string(&ntimer->command, name);

      /* retrieve old values */
      for (timer = otimers; timer ; timer = timer->next)
      {
          if (timer->id == ntimer->id)
          {
              if (timer->active && timer->cur_time < ntimer->set_time)
                  ntimer->cur_time = timer->cur_time;
              ntimer->active = timer->active;
          }
      }

      /* append to timers */
      if (!timers)
          timers = ntimer;
      else { 
	  for (timer = timers; timer->next ; timer = timer->next);
	  timer->next = ntimer;
      }
      create_timer(main_vbox, ntimer, 1);
    }

    for (timer = otimers; timer; timer = otimers) {
        otimers = timer->next;
        destroy_timer(timer);
    }

    list_modified = 0;
}


static void
reset_entries()
{
    gtk_entry_set_text(GTK_ENTRY(label_entry), "");
    // gtk_entry_set_text(GTK_ENTRY(hours_entry), "");
    // gtk_entry_set_text(GTK_ENTRY(mins_entry), "");
    // gtk_entry_set_text(GTK_ENTRY(secs_entry), "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(timer_radio), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stopwatch_radio), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(restart_toggle), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(popup_toggle), FALSE);
    gtk_entry_set_text(GTK_ENTRY(command_entry), "");
    selected_id = -1;
}


static void
cb_clist_selected(GtkWidget *clist, gint row, gint column,
		  GdkEventButton *bevent)
{
    gchar           *s;
    gint            state, i;

    i = 0;
    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    selected_id = atoi(s);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    gtk_entry_set_text(GTK_ENTRY(label_entry), s);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    gtk_entry_set_text(GTK_ENTRY(hours_spin), s);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    gtk_entry_set_text(GTK_ENTRY(mins_spin), s);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    gtk_entry_set_text(GTK_ENTRY(secs_spin), s);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    state = (strcmp(s, STOPWATCH) == 0) ? TRUE : FALSE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(timer_radio), !state);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stopwatch_radio), state);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    state = (strcmp(s, YES) == 0) ? TRUE : FALSE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(restart_toggle), state);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    state = (strcmp(s, YES) == 0) ? TRUE : FALSE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(popup_toggle), state);

    gtk_clist_get_text(GTK_CLIST(clist), row, i++, &s);
    gtk_entry_set_text(GTK_ENTRY(command_entry), s);

    selected_row = row;
}

static void
cb_clist_unselected(GtkWidget *clist, gint row, gint column,
		    GdkEventButton *bevent)
{
    reset_entries();
    selected_row = -1;
}

static void
cb_clist_up(GtkWidget *widget)
{
    gint            row;

    row = selected_row;
    if (row > 0)
    {
        gtk_clist_row_move(GTK_CLIST(timer_clist), row, row - 1);
        gtk_clist_select_row(GTK_CLIST(timer_clist), row - 1, -1);
        if (gtk_clist_row_is_visible(GTK_CLIST(timer_clist), row - 1)
            != GTK_VISIBILITY_FULL)
            gtk_clist_moveto(GTK_CLIST(timer_clist), row - 1, -1, 0.0, 0.0);
        selected_row = row - 1;
        list_modified = TRUE;
    }
}

static void
cb_clist_down(GtkWidget *widget)
{
    gint            row;

    row = selected_row;
    if (row >= 0 && row < GTK_CLIST(timer_clist)->rows - 1)
    {
        gtk_clist_row_move(GTK_CLIST(timer_clist), row, row + 1);
        gtk_clist_select_row(GTK_CLIST(timer_clist), row + 1, -1);
        if (gtk_clist_row_is_visible(GTK_CLIST(timer_clist), row + 1)
            != GTK_VISIBILITY_FULL)
            gtk_clist_moveto(GTK_CLIST(timer_clist), row + 1, -1, 1.0, 0.0);
        selected_row = row + 1;
        list_modified = TRUE;
    }
}

static void
cb_enter(GtkWidget *widget)
{
    gchar           *buf[CLIST_WIDTH];
    gint            i;

    if (selected_id < 0)
        selected_id = next_id++;
    i = 0;
    buf[i++] = g_strdup_printf("%d", selected_id);
    buf[i++] = gkrellm_entry_get_text(&label_entry);
    buf[i++] = gkrellm_entry_get_text(&hours_spin);
    buf[i++] = gkrellm_entry_get_text(&mins_spin);
    buf[i++] = gkrellm_entry_get_text(&secs_spin);
    buf[i++] = GTK_TOGGLE_BUTTON(stopwatch_radio)->active ? STOPWATCH : NOT_STOPWATCH;
    buf[i++] = GTK_TOGGLE_BUTTON(restart_toggle)->active ? YES : NO;
    buf[i++] = GTK_TOGGLE_BUTTON(popup_toggle)->active ? YES : NO;
    buf[i++] = gkrellm_entry_get_text(&command_entry);
    
    if (selected_row >= 0)
    {
        for (i = 0; i < CLIST_WIDTH; ++i)
            gtk_clist_set_text(GTK_CLIST(timer_clist), selected_row, i, buf[i]);
        gtk_clist_unselect_row(GTK_CLIST(timer_clist), selected_row, 0);
        selected_row = -1;
    }
    else
        gtk_clist_append(GTK_CLIST(timer_clist), buf);

    reset_entries();
    list_modified = TRUE;
}

static void
cb_delete(GtkWidget *widget)
{
    reset_entries();
    if (selected_row >= 0)
    {
        gtk_clist_remove(GTK_CLIST(timer_clist), selected_row);
        list_modified = TRUE;
        selected_row = -1;
    }
}

static void
cb_start(GtkWidget *widget)
{
    Timer *timer;

    if (selected_row < 0)
        return;
    for (timer = timers; timer ; timer = timer->next)
        if (timer->id == selected_id)
        {
            timer->active = TRUE;
            set_tooltip(timer);
        }
}

static void
cb_stop(GtkWidget *widget)
{
    Timer *timer;

    if (selected_row < 0)
        return;
    for (timer = timers; timer ; timer = timer->next)
        if (timer->id == selected_id)
        {
            timer->active = FALSE;
            set_tooltip(timer);
        }
}

static void
cb_reset(GtkWidget *widget)
{
    Timer *timer;

    if (selected_row < 0)
        return;
    for (timer = timers; timer ; timer = timer->next)
        if (timer->id == selected_id)
        {
            reset_timer(timer);
            set_tooltip(timer);
        }
}


static gchar    *plugin_info_text =
"This configuration tab is for the Timer/Stopwatch plugin.\n"
"\n"
"Adding new timers (count-down) or stopwatches (count-up) should be fairly easy.\n"
"A descriptive label is optional. It will only show up in the tooltip.\n"
"\n"
"Timer:\n"
"	The timer will count down from the given value until zero is reached.\n"
"\n"
"Stopwatch:\n"
"	The Stopwatch will count up starting at zero until the given value is reached.\n"
"\n"
"Restart:\n"
"	If set the timer will start counting down from the set value once it hits zero.\n"
"\n"
"Popup:\n"
"	Display a message window once the timer hits zero.\n"
"\n"
"You can use the following mouse clicks as shortcuts:\n"
"Left button:\n"
"	Start/Stop timer;\n"
"Middle button:\n"
"	Reset timer;\n"
"Right button:\n"
"	Open the configuration dialog.\n"
"\n"
"Please drop me a mail if you encounter problems or have questions.\n"
;

static gchar    *plugin_about_text =
   "Timer plugin 1.0\n"
   "GKrellM Timer Plugin\n\n"
   "Copyright (C) 2001 Christian W. Zuckschwerdt\n"
   "zany@triq.net\n\n"
   "http://triq.net/gkrellm.html\n\n"
   "Released under the GNU Public Licence"
;

static gchar *timer_title[CLIST_WIDTH] =
{ "ID", "Label", "Hours", "Minutes", "Seconds",
  "Up/Down", "Restart", "Popup", "Command" };

static void
create_plugin_tab(GtkWidget *tab_vbox)
{
    Timer *timer;
    struct tm *t;

    GtkWidget               *tabs;
    GtkWidget               *vbox;
    GtkWidget               *hbox;
    GtkWidget               *frame;
    GtkWidget               *hbox2;
    GtkWidget               *button;
    GtkWidget               *arrow;
    GtkWidget               *scrolled;
    GtkWidget               *text;
    GtkWidget               *label;

    gchar                   *buf[CLIST_WIDTH];
    gint                    row, i;

    /* Make a couple of tabs.  One for setup and one for info
     */
    tabs = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

/* --- Setup tab */
    vbox = gkrellm_create_tab(tabs, "Setup");

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(vbox), hbox);

    frame = gtk_frame_new ("Label");
    gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 2);
    gtk_widget_show (frame);

    label_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(label_entry), "");
    gtk_container_add(GTK_CONTAINER(frame), label_entry);


    frame = gtk_frame_new ("Time");
    gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 2);
    gtk_widget_show (frame);

    hbox2 = gtk_hbox_new(FALSE,0);
    gtk_container_add(GTK_CONTAINER(frame), hbox2);

    hours_spin_adj = gtk_adjustment_new (0, 0, 23, 1, 10, 10);
    hours_spin = gtk_spin_button_new (GTK_ADJUSTMENT (hours_spin_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox2),hours_spin,FALSE,FALSE,0);
    label = gtk_label_new(" : ");
    gtk_box_pack_start(GTK_BOX(hbox2),label,FALSE,FALSE,0);
    mins_spin_adj = gtk_adjustment_new (5, 0, 59, 1, 10, 10);
    mins_spin = gtk_spin_button_new (GTK_ADJUSTMENT (mins_spin_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox2),mins_spin,FALSE,FALSE,0);
    label = gtk_label_new(" : ");
    gtk_box_pack_start(GTK_BOX(hbox2),label,FALSE,FALSE,0);
    secs_spin_adj = gtk_adjustment_new (0, 0, 59, 1, 10, 10);
    secs_spin = gtk_spin_button_new (GTK_ADJUSTMENT (secs_spin_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox2),secs_spin,FALSE,FALSE,0);


    hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(vbox), hbox);

    frame = gtk_frame_new ("Options");
    gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 2);
    gtk_widget_show (frame);

    hbox2 = gtk_hbox_new(FALSE, 3);
    gtk_container_add(GTK_CONTAINER(frame), hbox2);

    timer_radio = gtk_radio_button_new_with_label(NULL, "Timer");
    stopwatch_radio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(timer_radio), "Stopwatch");
    gtk_box_pack_start(GTK_BOX(hbox2), timer_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), stopwatch_radio, FALSE, FALSE, 0);

    restart_toggle = gtk_check_button_new_with_label("Restart (Continuous)");
    gtk_box_pack_start(GTK_BOX(hbox2), restart_toggle, FALSE, FALSE, 0);

    popup_toggle = gtk_check_button_new_with_label("Popup");
    gtk_box_pack_start(GTK_BOX(hbox2), popup_toggle, FALSE, FALSE, 0);

    frame = gtk_frame_new ("Alarm command");
    gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 2);
    gtk_widget_show (frame);

    command_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(command_entry), "");
    gtk_container_add(GTK_CONTAINER(frame), command_entry);


    hbox = gtk_hbox_new(FALSE, 3);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

    start_button = gtk_button_new_with_label("Start");
    gtk_signal_connect(GTK_OBJECT(GTK_BUTTON(start_button)), "clicked",
                       GTK_SIGNAL_FUNC (cb_start), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), start_button, TRUE, TRUE, 4);
    stop_button = gtk_button_new_with_label("Stop");
    gtk_signal_connect(GTK_OBJECT(GTK_BUTTON(stop_button)), "clicked",
                       GTK_SIGNAL_FUNC (cb_stop), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), stop_button, TRUE, TRUE, 4);
    reset_button = gtk_button_new_with_label("Reset");
    gtk_signal_connect(GTK_OBJECT(GTK_BUTTON(reset_button)), "clicked",
                       GTK_SIGNAL_FUNC (cb_reset), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), reset_button, TRUE, TRUE, 4);

    button = gtk_button_new();
    arrow = gtk_arrow_new(GTK_ARROW_UP, GTK_SHADOW_ETCHED_OUT);
    gtk_container_add(GTK_CONTAINER(button), arrow);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       (GtkSignalFunc) cb_clist_up, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 4);

    button = gtk_button_new();
    arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_ETCHED_OUT);
    gtk_container_add(GTK_CONTAINER(button), arrow);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       (GtkSignalFunc) cb_clist_down, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 4);
    
    button = gtk_button_new_with_label("Enter");
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       (GtkSignalFunc) cb_enter, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 4);
    
    button = gtk_button_new_with_label("Delete");
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       (GtkSignalFunc) cb_delete, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 4);


    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    timer_clist = gtk_clist_new_with_titles(CLIST_WIDTH, timer_title);	
    gtk_clist_set_shadow_type (GTK_CLIST(timer_clist), GTK_SHADOW_OUT);
    gtk_clist_set_column_width (GTK_CLIST(timer_clist), 1, 100);
    gtk_clist_set_column_width (GTK_CLIST(timer_clist), 8, 200);

    gtk_signal_connect(GTK_OBJECT(timer_clist), "select_row",
                       (GtkSignalFunc) cb_clist_selected, NULL);
    gtk_signal_connect(GTK_OBJECT(timer_clist), "unselect_row",
                       (GtkSignalFunc) cb_clist_unselected, NULL);

    gtk_container_add(GTK_CONTAINER(scrolled), timer_clist);

        for (timer = timers; timer; timer = timer->next)
        {
            t = my_localtime( &timer->set_time );
            i = 0;
            buf[i++] = g_strdup_printf("%d", timer->id);
            buf[i++] = timer->label;
            buf[i++] = g_strdup_printf("%d", t->tm_hour);
            buf[i++] = g_strdup_printf("%d", t->tm_min);
            buf[i++] = g_strdup_printf("%d", t->tm_sec);
            buf[i++] = timer->stopwatch ? STOPWATCH : NOT_STOPWATCH;
            buf[i++] = timer->restart ? YES : NO;
            buf[i++] = timer->popup ? YES : NO;
            buf[i++] = timer->command;
            row = gtk_clist_append(GTK_CLIST(timer_clist), buf);
        }

/* --- Info tab */
        vbox = gkrellm_create_tab(tabs, "Info");
        scrolled = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
        text = gtk_text_new(NULL, NULL);
        gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL, plugin_info_text, -1);
        gtk_text_set_editable(GTK_TEXT(text), FALSE);
        gtk_container_add(GTK_CONTAINER(scrolled), text);

/* --- about text */

	text = gtk_label_new(plugin_about_text); 

	gtk_notebook_append_page(GTK_NOTEBOOK(tabs), text,
				 gtk_label_new("About"));

}


static Monitor  plugin_mon  =
        {
        CONFIG_NAME,           /* Name, for config tab.        */
        0,                     /* Id,  0 if a plugin           */
        create_plugin,         /* The create_plugin() function */
        update_plugin,         /* The update_plugin() function */
        create_plugin_tab,     /* The create_plugin_tab() config function */
        apply_plugin_config,   /* The apply_plugin_config() function      */

        save_plugin_config,    /* The save_plugin_config() function  */
        load_plugin_config,    /* The load_plugin_config() function  */
        PLUGIN_CONFIG_KEYWORD, /* config keyword                     */

        NULL,                  /* Undefined 2  */
        NULL,                  /* Undefined 1  */
        NULL,                  /* Undefined 0  */

        MON_CLOCK|MON_INSERT_AFTER, /* Insert plugin before this monitor.  */
        NULL,                  /* Handle if a plugin, filled in by GKrellM */
        NULL                   /* path if a plugin, filled in by GKrellM   */
        };

Monitor *
init_plugin(void)
{
    timers = NULL;

    style_id = gkrellm_add_meter_style(&plugin_mon, STYLE_NAME);

    plugin_mon_ref = &plugin_mon;
    return &plugin_mon;
}
