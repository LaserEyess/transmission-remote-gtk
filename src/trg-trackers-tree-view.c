/*
 * transmission-remote-gtk - Transmission RPC client for GTK
 * Copyright (C) 2011  Alan Fitton

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gtk/gtk.h>

#include "trg-trackers-tree-view.h"
#include "trg-tree-view.h"
#include "trg-client.h"
#include "trg-menu-bar.h"
#include "requests.h"
#include "dispatch.h"
#include "json.h"
#include "trg-trackers-model.h"
#include "trg-main-window.h"

G_DEFINE_TYPE(TrgTrackersTreeView, trg_trackers_tree_view,
	      TRG_TYPE_TREE_VIEW)

#define TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRG_TYPE_TRACKERS_TREE_VIEW, TrgTrackersTreeViewPrivate))

typedef struct _TrgTrackersTreeViewPrivate TrgTrackersTreeViewPrivate;

struct _TrgTrackersTreeViewPrivate {
    trg_client *client;
    GtkCellRenderer *announceRenderer;
    TrgMainWindow *win;
};

static void
trg_trackers_tree_view_class_init(TrgTrackersTreeViewClass *klass)
{
	g_type_class_add_private(klass, sizeof(TrgTrackersTreeViewPrivate));
}

static void
trg_trackers_tree_view_json_id_array_foreach(GtkTreeModel * model,
					    GtkTreePath *
					    path G_GNUC_UNUSED,
					    GtkTreeIter * iter,
					    gpointer data)
{
    JsonArray *output = (JsonArray *) data;
    gint64 id;
    gtk_tree_model_get(model, iter, TRACKERCOL_ID, &id, -1);
    json_array_add_int_element(output, id);
}

static JsonArray *trackers_build_json_id_array(TrgTrackersTreeView * tv)
{
    GtkTreeSelection *selection =
	gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));

    JsonArray *ids = json_array_new();
    gtk_tree_selection_selected_foreach(selection,
					(GtkTreeSelectionForeachFunc)
					trg_trackers_tree_view_json_id_array_foreach,
					ids);

    if (json_array_get_length(ids) < 1)
    {
    	json_array_unref(ids);
    	return NULL;
    }

    return ids;
}

void trg_trackers_tree_view_new_connection(TrgTrackersTreeView *tv, trg_client *tc)
{
	TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(tv);
	gboolean editable = trg_client_supports_tracker_edit(tc);

	g_object_set(priv->announceRenderer, "editable", editable, NULL);
	g_object_set(priv->announceRenderer, "mode", editable ? GTK_CELL_RENDERER_MODE_EDITABLE : GTK_CELL_RENDERER_MODE_INERT, NULL);
}

static void trg_tracker_announce_edited(GtkCellRendererText *renderer,
                                                        gchar               *path,
                                                        gchar               *new_text,
                                                        gpointer             user_data)
{
	TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(user_data);
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(user_data));
	gint64 torrentId = trg_trackers_model_get_torrent_id(TRG_TRACKERS_MODEL(model));
	JsonArray *torrentIds = json_array_new();
	JsonArray *modPairs = json_array_new();

	gint64 trackerId;
	JsonNode *req;
	JsonObject *args;
	GtkTreeIter iter;

	gtk_tree_model_get_iter_from_string(model, &iter, path);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, TRACKERCOL_ANNOUNCE, new_text, -1);
	gtk_tree_model_get(model, &iter, TRACKERCOL_ID, &trackerId, -1);

	json_array_add_int_element(torrentIds, torrentId);

	req = torrent_set(torrentIds);
	args = node_get_arguments(req);

	json_array_add_int_element(modPairs, trackerId);
	json_array_add_string_element(modPairs, new_text);

	json_object_set_array_member(args, "trackerReplace", modPairs);

	trg_trackers_model_set_update_barrier(TRG_TRACKERS_MODEL(model), priv->client->updateSerial+1);

	dispatch_async(priv->client, req, on_generic_interactive_action, priv->win);
}

static void trg_tracker_announce_editing_started(GtkCellRenderer *renderer,
        GtkCellEditable *editable,
        gchar           *path,
        gpointer         user_data)
{
	TrgTrackersModel *model = TRG_TRACKERS_MODEL(gtk_tree_view_get_model(GTK_TREE_VIEW(user_data)));
	trg_trackers_model_set_update_barrier(model, TRACKERS_UPDATE_BARRIER_FULL);
}

static void trg_tracker_announce_editing_canceled(GtkWidget *w, gpointer data)
{
	TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(data);
	TrgTrackersModel *model = TRG_TRACKERS_MODEL(gtk_tree_view_get_model(GTK_TREE_VIEW(data)));

	trg_trackers_model_set_update_barrier(model, priv->client->updateSerial+1);
}

static void trg_trackers_tree_view_init(TrgTrackersTreeView * self)
{
	TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(self);

    trg_tree_view_add_pixbuf_text_column(TRG_TREE_VIEW(self),
					 TRACKERCOL_ICON,
					 TRACKERCOL_TIER, "Tier", -1);

    priv->announceRenderer = trg_tree_view_add_column(TRG_TREE_VIEW(self), "Announce URL",
			     TRACKERCOL_ANNOUNCE);
    g_signal_connect(priv->announceRenderer, "edited", G_CALLBACK(trg_tracker_announce_edited), self);
    g_signal_connect(priv->announceRenderer, "editing-canceled", G_CALLBACK(trg_tracker_announce_editing_canceled), self);
    g_signal_connect(priv->announceRenderer, "editing-started", G_CALLBACK(trg_tracker_announce_editing_started), self);

    trg_tree_view_add_column(TRG_TREE_VIEW(self), "Scrape URL",
			     TRACKERCOL_SCRAPE);
}

static void add_tracker(GtkWidget *w, gpointer data)
{
	GtkTreeView *tv = GTK_TREE_VIEW(data);
	GtkTreeModel *model = gtk_tree_view_get_model(tv);
	GtkTreeIter iter;

	gtk_list_store_append(GTK_LIST_STORE(model), &iter);
}

static void delete_tracker(GtkWidget *w, gpointer data)
{
	TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(data);
	TrgTrackersTreeView *tv = TRG_TRACKERS_TREE_VIEW(data);
	TrgTrackersModel *model = TRG_TRACKERS_MODEL(gtk_tree_view_get_model(GTK_TREE_VIEW(data)));

	JsonArray *trackerIds, *torrentIds;
	JsonNode *req;
	JsonObject *args;

	if ((trackerIds = trackers_build_json_id_array(tv)) == NULL)
		return;

	torrentIds = json_array_new();
	json_array_add_int_element(torrentIds, trg_trackers_model_get_torrent_id(model));

	req = torrent_set(torrentIds);

	args = node_get_arguments(req);

	json_object_set_array_member(args, "trackerRemove", trackerIds);

	dispatch_async(priv->client, req, on_generic_interactive_action, priv->win);
}

static void
view_popup_menu_add_only(GtkWidget * treeview, GdkEventButton * event,
		gpointer data G_GNUC_UNUSED)
{
    GtkWidget *menu, *menuitem;

    menu = gtk_menu_new();

    menuitem = trg_menu_bar_item_new(GTK_MENU_SHELL(menu), "Add", GTK_STOCK_ADD, TRUE);
    g_signal_connect(menuitem, "activate", G_CALLBACK(add_tracker), treeview);

    gtk_widget_show_all(menu);

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		   (event != NULL) ? event->button : 0,
		   gdk_event_get_time((GdkEvent *) event));
}

static void
view_popup_menu(GtkWidget * treeview, GdkEventButton * event,
		gpointer data G_GNUC_UNUSED)
{
    GtkWidget *menu, *menuitem;

    menu = gtk_menu_new();

    menuitem = trg_menu_bar_item_new(GTK_MENU_SHELL(menu), "Delete", GTK_STOCK_DELETE, TRUE);
    g_signal_connect(menuitem, "activate", G_CALLBACK(delete_tracker), treeview);

    menuitem = trg_menu_bar_item_new(GTK_MENU_SHELL(menu), "Add", GTK_STOCK_ADD, TRUE);
    g_signal_connect(menuitem, "activate", G_CALLBACK(add_tracker), treeview);

    gtk_widget_show_all(menu);

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		   (event != NULL) ? event->button : 0,
		   gdk_event_get_time((GdkEvent *) event));
}

static gboolean
view_onButtonPressed(GtkWidget * treeview, GdkEventButton * event,
		     gpointer userdata)
{
	TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(treeview);
	TrgTrackersModel *model = TRG_TRACKERS_MODEL(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));
    GtkTreeSelection *selection;
    GtkTreePath *path;

    if (!trg_client_supports_tracker_edit(priv->client))
    	return FALSE;

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
					  (gint) event->x,
					  (gint) event->y, &path,
					  NULL, NULL, NULL)) {
	    if (!gtk_tree_selection_path_is_selected(selection, path)) {
		gtk_tree_selection_unselect_all(selection);
		gtk_tree_selection_select_path(selection, path);
	    }
	    gtk_tree_path_free(path);

	    view_popup_menu(treeview, event, userdata);
	    return TRUE;
	} else if (trg_trackers_model_get_torrent_id(model) >= 0) {
    	view_popup_menu_add_only(treeview, event, userdata);
    }
    }

    return FALSE;
}

static gboolean view_onPopupMenu(GtkWidget * treeview, gpointer userdata)
{
    view_popup_menu(treeview, NULL, userdata);
    return TRUE;
}

TrgTrackersTreeView *trg_trackers_tree_view_new(TrgTrackersModel * model, trg_client *client, TrgMainWindow *win)
{
    GObject *obj = g_object_new(TRG_TYPE_TRACKERS_TREE_VIEW, NULL);
    TrgTrackersTreeViewPrivate *priv = TRG_TRACKERS_TREE_VIEW_GET_PRIVATE(obj);

    g_signal_connect(obj, "button-press-event",
		     G_CALLBACK(view_onButtonPressed), NULL);
    g_signal_connect(obj, "popup-menu", G_CALLBACK(view_onPopupMenu),
		     NULL);

    gtk_tree_view_set_model(GTK_TREE_VIEW(obj), GTK_TREE_MODEL(model));
    priv->client = client;
    priv->win = win;

    return TRG_TRACKERS_TREE_VIEW(obj);
}
