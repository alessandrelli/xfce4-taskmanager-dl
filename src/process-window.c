/*
 * Copyright (c) 2010 Mike Massonnet, <mmassonnet@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "settings.h"
#include "process-window.h"
#include "process-window_ui.h"
#include "process-monitor.h"
#include "process-tree-view.h"
#include "process-statusbar.h"
#include "exec-tool-button.h"
#include "settings-tool-button.h"



typedef struct _XtmProcessWindowClass XtmProcessWindowClass;
struct _XtmProcessWindowClass
{
	GtkWidgetClass		parent_class;
};
struct _XtmProcessWindow
{
	GtkWidget		parent;
	/*<private>*/
	GtkBuilder *		builder;
	GtkWidget *		window;
	GtkWidget *		toolbar;
	GtkWidget *		cpu_monitor;
	GtkWidget *		mem_monitor;
	GtkWidget *		treeview;
	GtkWidget *		statusbar;
	GtkWidget *		exec_button;
	GtkWidget *		settings_button;
	XtmSettings *		settings;
};
G_DEFINE_TYPE (XtmProcessWindow, xtm_process_window, GTK_TYPE_WIDGET)

static void	xtm_process_window_finalize			(GObject *object);
static void	xtm_process_window_show				(GtkWidget *widget);
static void	xtm_process_window_hide				(GtkWidget *widget);

static void	emit_destroy_signal				(XtmProcessWindow *window);
static gboolean	emit_delete_event_signal			(XtmProcessWindow *window, GdkEvent *event);
static void	toolbar_update_style				(XtmProcessWindow *window);
static void	monitor_update_step_size			(XtmProcessWindow *window);
static void	monitor_update_paint_box			(XtmProcessWindow *window);
static void	show_about_dialog				(XtmProcessWindow *window);



static void
xtm_process_window_class_init (XtmProcessWindowClass *klass)
{
	GObjectClass *class;
	GtkWidgetClass *widget_class;

	xtm_process_window_parent_class = g_type_class_peek_parent (klass);
	class = G_OBJECT_CLASS (klass);
	class->finalize = xtm_process_window_finalize;
	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->show = xtm_process_window_show;
	widget_class->hide = xtm_process_window_hide;
}

static void
xtm_process_window_init (XtmProcessWindow *window)
{
	GtkWidget *button;
	gint width, height;

	window->settings = xtm_settings_get_default ();

	window->builder = gtk_builder_new ();
	gtk_builder_add_from_string (window->builder, process_window_ui, process_window_ui_length, NULL);

	window->window = GTK_WIDGET (gtk_builder_get_object (window->builder, "process-window"));
	g_object_get (window->settings, "window-width", &width, "window-height", &height, NULL);
	if (width >= 1 && height >= 1)
		gtk_window_resize (GTK_WINDOW (window->window), width, height);
	g_signal_connect_swapped (window->window, "destroy", G_CALLBACK (emit_destroy_signal), window);
	g_signal_connect_swapped (window->window, "delete-event", G_CALLBACK (emit_delete_event_signal), window);

	window->toolbar = GTK_WIDGET (gtk_builder_get_object (window->builder, "process-toolbar"));
	g_signal_connect_swapped (window->settings, "notify::toolbar-style", G_CALLBACK (toolbar_update_style), window);
	g_object_notify (G_OBJECT (window->settings), "toolbar-style");

	window->exec_button = xtm_exec_tool_button_new ();
	gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), GTK_TOOL_ITEM (window->exec_button), 0);

	window->settings_button = xtm_settings_tool_button_new ();
	gtk_toolbar_insert (GTK_TOOLBAR (window->toolbar), GTK_TOOL_ITEM (window->settings_button), 1);

	{
		GtkWidget *toolitem;
		guint refresh_rate;

		g_object_get (window->settings, "refresh-rate", &refresh_rate, NULL);

		toolitem = GTK_WIDGET (gtk_builder_get_object (window->builder, "cpu-toolitem"));
		window->cpu_monitor = xtm_process_monitor_new ();
		xtm_process_monitor_set_step_size (XTM_PROCESS_MONITOR (window->cpu_monitor), refresh_rate / 1000.0);
		gtk_widget_show (window->cpu_monitor);
		gtk_container_add (GTK_CONTAINER (toolitem), window->cpu_monitor);

		toolitem = GTK_WIDGET (gtk_builder_get_object (window->builder, "mem-toolitem"));
		window->mem_monitor = xtm_process_monitor_new ();
		xtm_process_monitor_set_step_size (XTM_PROCESS_MONITOR (window->mem_monitor), refresh_rate / 1000.0);
		gtk_widget_show (window->mem_monitor);
		gtk_container_add (GTK_CONTAINER (toolitem), window->mem_monitor);

		monitor_update_paint_box (window);
		g_signal_connect_swapped (window->settings, "notify::monitor-paint-box", G_CALLBACK (monitor_update_paint_box), window);
		g_signal_connect_swapped (window->settings, "notify::refresh-rate", G_CALLBACK (monitor_update_step_size), window);
	}

	button = GTK_WIDGET (gtk_builder_get_object (window->builder, "toolbutton-about"));
	g_signal_connect_swapped (button, "clicked", G_CALLBACK (show_about_dialog), window);

	if (geteuid () == 0)
	{
		gtk_rc_parse_string ("style\"root-warning-style\"{bg[NORMAL]=\"#b4254b\"\nfg[NORMAL]=\"#fefefe\"}\n"
				"widget\"GtkWindow.*.root-warning\"style\"root-warning-style\"\n"
				"widget\"GtkWindow.*.root-warning.GtkLabel\"style\"root-warning-style\"");
		gtk_widget_set_name (GTK_WIDGET (gtk_builder_get_object (window->builder, "root-warning-ebox")), "root-warning");
		gtk_widget_show_all (GTK_WIDGET (gtk_builder_get_object (window->builder, "root-warning-box")));
	}

	window->treeview = xtm_process_tree_view_new ();
	gtk_widget_show (window->treeview);
	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (window->builder, "scrolledwindow")), window->treeview);

	window->statusbar = xtm_process_statusbar_new ();
	gtk_widget_show (window->statusbar);
	gtk_box_pack_start (GTK_BOX (gtk_builder_get_object (window->builder, "process-vbox")), window->statusbar, FALSE, FALSE, 0);

	g_object_unref (window->builder);
	window->builder = NULL;
}

static void
xtm_process_window_finalize (GObject *object)
{
	XtmProcessWindow *window = XTM_PROCESS_WINDOW (object);

	if (GTK_IS_WINDOW (window->window))
	{
		gint width, height;
		guint sort_column_id;
		GtkSortType sort_type;

		gtk_window_get_size (GTK_WINDOW (window->window), &width, &height);
		xtm_process_tree_view_get_sort_column_id (XTM_PROCESS_TREE_VIEW (window->treeview), (gint*)&sort_column_id, &sort_type);

		g_object_set (window->settings, "window-width", width, "window-height", height,
				"sort-column-id", sort_column_id, "sort-type", sort_type, NULL);
	}

	if (GTK_IS_TREE_VIEW (window->treeview))
		gtk_widget_destroy (window->treeview);

	if (GTK_IS_STATUSBAR (window->statusbar))
		gtk_widget_destroy (window->statusbar);

	if (GTK_IS_TOOL_ITEM (window->exec_button))
		gtk_widget_destroy (window->exec_button);

	if (GTK_IS_TOOL_ITEM (window->settings_button))
		gtk_widget_destroy (window->settings_button);

	if (XTM_IS_SETTINGS (window->settings))
		g_object_unref (window->settings);
}

/**
 * Helper functions
 */

static void
emit_destroy_signal (XtmProcessWindow *window)
{
	g_signal_emit_by_name (window, "destroy", G_TYPE_NONE);
}

static gboolean
emit_delete_event_signal (XtmProcessWindow *window, GdkEvent *event)
{
	gboolean ret;
	g_signal_emit_by_name (window, "delete-event", event, &ret, G_TYPE_BOOLEAN);
	return ret;
}

static void
toolbar_update_style (XtmProcessWindow *window)
{
	XtmToolbarStyle toolbar_style;
	g_object_get (window->settings, "toolbar-style", &toolbar_style, NULL);
	switch (toolbar_style)
	{
		default:
		case XTM_TOOLBAR_STYLE_DEFAULT:
		gtk_toolbar_set_icon_size (GTK_TOOLBAR (window->toolbar), GTK_ICON_SIZE_MENU);
		gtk_toolbar_unset_style (GTK_TOOLBAR (window->toolbar));
		break;

		case XTM_TOOLBAR_STYLE_SMALL:
		gtk_toolbar_set_icon_size (GTK_TOOLBAR (window->toolbar), GTK_ICON_SIZE_SMALL_TOOLBAR);
		gtk_toolbar_set_style (GTK_TOOLBAR (window->toolbar), GTK_TOOLBAR_ICONS);
		break;

		case XTM_TOOLBAR_STYLE_LARGE:
		gtk_toolbar_set_icon_size (GTK_TOOLBAR (window->toolbar), GTK_ICON_SIZE_LARGE_TOOLBAR);
		gtk_toolbar_set_style (GTK_TOOLBAR (window->toolbar), GTK_TOOLBAR_ICONS);
		break;

		case XTM_TOOLBAR_STYLE_TEXT:
		gtk_toolbar_set_icon_size (GTK_TOOLBAR (window->toolbar), GTK_ICON_SIZE_MENU);
		gtk_toolbar_set_style (GTK_TOOLBAR (window->toolbar), GTK_TOOLBAR_BOTH);
		break;
	}
}

static void
monitor_update_paint_box (XtmProcessWindow *window)
{
	gboolean paint_box;
	g_object_get (window->settings, "monitor-paint-box", &paint_box, NULL);
	xtm_process_monitor_set_paint_box (XTM_PROCESS_MONITOR (window->cpu_monitor), paint_box);
	xtm_process_monitor_set_paint_box (XTM_PROCESS_MONITOR (window->mem_monitor), paint_box);
}

static void
monitor_update_step_size (XtmProcessWindow *window)
{
	guint refresh_rate;
	g_object_get (window->settings, "refresh-rate", &refresh_rate, NULL);
	g_object_set (window->cpu_monitor, "step-size", refresh_rate / 1000.0, NULL);
	g_object_set (window->mem_monitor, "step-size", refresh_rate / 1000.0, NULL);
}

#if !GTK_CHECK_VERSION(2,18,0)
static void
url_hook_about_dialog (GtkAboutDialog *dialog, const gchar *uri, gpointer user_data)
{
	gchar *command = g_strdup_printf ("exo-open %s", uri);
	if (!g_spawn_command_line_async (command, NULL))
	{
		g_free (command);
		command = g_strdup_printf ("firefox %s", uri);
		g_spawn_command_line_async (command, NULL);
	}
	g_free (command);
}
#endif

static void
show_about_dialog (XtmProcessWindow *window)
{
	const gchar *authors[] = {
		"(c) 2008-2010 Mike Massonnet",
		"(c) 2005-2008 Johannes Zellner",
		"",
		"FreeBSD",
		"  \342\200\242 Mike Massonnet",
		"  \342\200\242 Oliver Lehmann",
		"",
		"OpenBSD",
		"  \342\200\242 Landry Breuil",
		"",
		"Linux",
		"  \342\200\242 Johannes Zellner",
		"  \342\200\242 Mike Massonnet",
		"",
		"OpenSolaris",
		"  \342\200\242 Mike Massonnet",
		"  \342\200\242 Peter Tribble",
		NULL };
	const gchar *license =
		"This program is free software; you can redistribute it and/or modify\n"
		"it under the terms of the GNU General Public License as published by\n"
		"the Free Software Foundation; either version 2 of the License, or\n"
		"(at your option) any later version.\n";

#if !GTK_CHECK_VERSION(2,18,0)
	gtk_about_dialog_set_url_hook (url_hook_about_dialog, NULL, NULL);
#endif
	gtk_show_about_dialog (GTK_WINDOW (window->window),
		"program-name", _("Task Manager"),
		"version", PACKAGE_VERSION,
		"copyright", "Copyright \302\251 2005-2010 The Xfce development team",
		"logo-icon-name", "utilities-system-monitor",
		"icon-name", GTK_STOCK_ABOUT,
		"comments", _("Easy to use task manager"),
		"license", license,
		"authors", authors,
		"translator-credits", _("translator-credits"),
		"website", "http://goodies.xfce.org/projects/applications/xfce4-taskmanager",
		"website-label", "goodies.xfce.org",
		NULL);
}



/**
 * Class functions
 */

GtkWidget *
xtm_process_window_new (void)
{
	return g_object_new (XTM_TYPE_PROCESS_WINDOW, NULL);
}

static void
xtm_process_window_show (GtkWidget *widget)
{
	g_return_if_fail (GTK_IS_WIDGET (widget));
	g_return_if_fail (GTK_IS_WIDGET (XTM_PROCESS_WINDOW (widget)->window));
	gtk_widget_show (XTM_PROCESS_WINDOW (widget)->window);
	gtk_window_present (GTK_WINDOW (XTM_PROCESS_WINDOW (widget)->window));
	GTK_WIDGET_SET_FLAGS (widget, GTK_VISIBLE);
}

static void
xtm_process_window_hide (GtkWidget *widget)
{
	gint winx, winy;
	g_return_if_fail (GTK_IS_WIDGET (widget));
	if (!GTK_IS_WIDGET (XTM_PROCESS_WINDOW (widget)->window))
		return;
	gtk_window_get_position (GTK_WINDOW (XTM_PROCESS_WINDOW (widget)->window), &winx, &winy);
	gtk_widget_hide (XTM_PROCESS_WINDOW (widget)->window);
	gtk_window_move (GTK_WINDOW (XTM_PROCESS_WINDOW (widget)->window), winx, winy);
	GTK_WIDGET_UNSET_FLAGS (widget, GTK_VISIBLE);
}

GtkTreeModel *
xtm_process_window_get_model (XtmProcessWindow *window)
{
	g_return_val_if_fail (XTM_IS_PROCESS_WINDOW (window), NULL);
	g_return_val_if_fail (GTK_IS_TREE_VIEW (window->treeview), NULL);
	return gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (GTK_TREE_VIEW (window->treeview))));
}

void
xtm_process_window_set_system_info (XtmProcessWindow *window, guint num_processes, gfloat cpu, gfloat memory, gfloat swap)
{
	gchar text[100];
	gchar value[4];

	g_return_if_fail (XTM_IS_PROCESS_WINDOW (window));
	g_return_if_fail (GTK_IS_STATUSBAR (window->statusbar));

	g_object_set (window->statusbar, "num-processes", num_processes, "cpu", cpu, "memory", memory, "swap", swap, NULL);

	xtm_process_monitor_add_peak (XTM_PROCESS_MONITOR (window->cpu_monitor), cpu / 100.0);
	g_snprintf (value, 4, "%.0f", cpu);
	g_snprintf (text, 100, _("CPU: %s%%"), value);
	gtk_widget_set_tooltip_text (window->cpu_monitor, text);

	xtm_process_monitor_add_peak (XTM_PROCESS_MONITOR (window->mem_monitor), memory / 100.0);
	g_snprintf (value, 4, "%.0f", memory);
	g_snprintf (text, 100, _("Memory: %s%%"), value);
	gtk_widget_set_tooltip_text (window->mem_monitor, text);
}

void
xtm_process_window_show_swap_usage (XtmProcessWindow *window, gboolean show_swap_usage)
{
	g_return_if_fail (XTM_IS_PROCESS_WINDOW (window));
	g_return_if_fail (GTK_IS_STATUSBAR (window->statusbar));
	g_object_set (window->statusbar, "show-swap", show_swap_usage, NULL);
}
