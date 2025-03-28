/********************************************************************\
 * dialog-imap-editor.c -- Import Map Editor dialog                 *
 * Copyright (C) 2015 Robert Fewell                                 *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "dialog-imap-editor.h"

#include "dialog-utils.h"
#include "gnc-component-manager.h"
#include "gnc-session.h"

#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include <gnc-glib-utils.h>
#include "Account.h"

#define DIALOG_IMAP_CM_CLASS    "dialog-imap-edit"
#define GNC_PREFS_GROUP         "dialogs.imap-editor"

#define IMAP_FRAME_BAYES        "import-map-bayes"
#define IMAP_FRAME              "import-map"

#define IMAP_FRAME_DESC         "desc"
#define IMAP_FRAME_MEMO         "memo"
#define IMAP_FRAME_CSV          "csv-account-map"

/** Enumeration for the tree-store */
enum GncImapColumn {SOURCE_FULL_ACC, SOURCE_ACCOUNT, BASED_ON, MATCH_STRING,
                     MAP_FULL_ACC, MAP_ACCOUNT, HEAD, CATEGORY, COUNT, FILTER};

typedef enum
{
    BAYES,
    NBAYES,
    ONLINE
}GncListType;

typedef struct
{
    guint inv_dialog_shown_bayes : 1;
    guint inv_dialog_shown_nbayes : 1;
    guint inv_dialog_shown_online : 1;
}GncInvFlags;

typedef struct
{
    GtkWidget    *dialog;
    QofSession   *session;
    GtkWidget    *view;
    GtkTreeModel *model;
    GncListType   type;

    GtkWidget    *radio_bayes;
    GtkWidget    *radio_nbayes;
    GtkWidget    *radio_online;

    GtkWidget    *filter_button;
    GtkWidget    *filter_text_entry;
    GtkWidget    *filter_label;
    gboolean      apply_selection_filter;

    GtkWidget    *total_entries_label;
    gint          tot_entries;
    gint          tot_invalid_maps;

    GtkWidget    *expand_button;
    GtkWidget    *collapse_button;
    GtkWidget    *remove_button;
    GncInvFlags   inv_dialog_shown;
}ImapDialog;


/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_GUI;

void gnc_imap_dialog_window_destroy_cb (GtkWidget *object, gpointer user_data);
void gnc_imap_dialog_close_cb (GtkDialog *dialog, gpointer user_data);
void gnc_imap_dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data);

static void get_account_info (ImapDialog *imap_dialog);

void
gnc_imap_dialog_window_destroy_cb (GtkWidget *object, gpointer user_data)
{
    ImapDialog *imap_dialog = user_data;

    ENTER(" ");
    gnc_unregister_gui_component_by_data (DIALOG_IMAP_CM_CLASS, imap_dialog);

    if (imap_dialog->dialog)
    {
        gtk_widget_destroy (imap_dialog->dialog);
        imap_dialog->dialog = NULL;
    }
    g_free (imap_dialog);
    LEAVE(" ");
}

void
gnc_imap_dialog_close_cb (GtkDialog *dialog, gpointer user_data)
{
    ImapDialog *imap_dialog = user_data;

    ENTER(" ");
    gnc_close_gui_component_by_data (DIALOG_IMAP_CM_CLASS, imap_dialog);
    LEAVE(" ");
}

static void
delete_info_bayes (Account *source_account, gchar *head, gint depth)
{
    if (depth != 1) // below top level
        gnc_account_delete_map_entry (source_account, head, NULL, NULL, FALSE);
    else
        gnc_account_delete_all_bayes_maps (source_account);
}

static void
delete_info_nbayes (Account *source_account, gchar *head,
                    gchar *category, gchar *match_string, gint depth)
{
    if (depth != 1) // below top level
    {
        gnc_account_delete_map_entry (source_account, head, category, match_string, FALSE);
        gnc_account_delete_map_entry (source_account, head, category, NULL, TRUE);
    }
    else
        gnc_account_delete_map_entry (source_account, head, category, NULL, FALSE);

    gnc_account_delete_map_entry (source_account, head, NULL, NULL, TRUE);
}

static void
delete_selected_row (ImapDialog *imap_dialog, GtkTreeIter *iter)
{
    Account     *source_account = NULL;
    gchar       *full_source_account;
    gchar       *head;
    gchar       *category;
    gchar       *match_string;
    gint         num = 0;
    GtkTreeIter  parent;

    // get the parent iter and see how many children it has, if 1 we will remove
    if (gtk_tree_model_iter_parent (imap_dialog->model, &parent, iter))
        num = gtk_tree_model_iter_n_children (imap_dialog->model, &parent);

    gtk_tree_model_get (imap_dialog->model, iter, SOURCE_ACCOUNT, &source_account,
                                                  SOURCE_FULL_ACC, &full_source_account,
                                                  HEAD, &head,
                                                  CATEGORY, &category,
                                                  MATCH_STRING, &match_string, -1);

    PINFO("Account is '%s', Head is '%s', Category is '%s', Match String is '%s'",
           full_source_account, head, category, match_string);

    if (source_account != NULL)
    {
        GtkTreePath *tree_path;
        gint         depth;

        // Get the level we are at in the tree-model
        tree_path = gtk_tree_model_get_path (imap_dialog->model, iter);
        depth = gtk_tree_path_get_depth (tree_path);
        gtk_tree_path_free (tree_path);

        if (imap_dialog->type == ONLINE)
            gnc_account_delete_map_entry (source_account, head, NULL, NULL, FALSE);

        if (imap_dialog->type == BAYES)
            delete_info_bayes (source_account, head, depth);

        if (imap_dialog->type == NBAYES)
            delete_info_nbayes (source_account, head, category, match_string, depth);

        gtk_tree_store_remove (GTK_TREE_STORE(imap_dialog->model), iter);

        if (num == 1 && (imap_dialog->type != ONLINE))
            gtk_tree_store_remove (GTK_TREE_STORE(imap_dialog->model), &parent);
    }
    // Clear the total
    gtk_label_set_text (GTK_LABEL(imap_dialog->total_entries_label), " ");

    if (head)
        g_free (head);
    if (category)
        g_free (category);
    if (match_string)
        g_free (match_string);
    if (full_source_account)
        g_free (full_source_account);
}

static gboolean
find_invalid_mappings_total (GtkTreeModel *model, GtkTreePath *path,
                             GtkTreeIter *iter, ImapDialog *imap_dialog)
{
    Account *source_account = NULL;
    Account *map_account = NULL;
    gchar   *head;
    gint     depth;

    gtk_tree_model_get (model, iter, SOURCE_ACCOUNT, &source_account,
                                     MAP_ACCOUNT, &map_account,
                                     HEAD, &head, -1);

    depth = gtk_tree_path_get_depth (path);

    if ((source_account != NULL) && (map_account == NULL))
    {
        if (((g_strcmp0 (head, "online_id") == 0) && (depth == 1)) || (depth == 2))
            imap_dialog->tot_invalid_maps ++;
    }
    g_free (head);
    return FALSE;
}

static void
gnc_imap_dialog_delete (ImapDialog *imap_dialog)
{
    GList            *list, *row;
    GtkTreeModel     *fmodel;
    GtkTreeIter       fiter;
    GtkTreeIter       iter;
    GtkTreeSelection *selection;

    fmodel = gtk_tree_view_get_model (GTK_TREE_VIEW(imap_dialog->view));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(imap_dialog->view));

    list = gtk_tree_selection_get_selected_rows (selection, &fmodel);

    // Make sure we have some rows selected
    if (!gnc_list_length_cmp (list, 0))
        return;

    // reset the invalid map total
    imap_dialog->tot_invalid_maps = 0;

    // reverse list
    list = g_list_reverse (list);

    // Suspend GUI refreshing
    gnc_suspend_gui_refresh();

    // Walk the list
    for (row = g_list_first (list); row; row = g_list_next (row))
    {
        if (gtk_tree_model_get_iter (fmodel, &fiter, row->data))
        {
            gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER(fmodel), &iter, &fiter);
            delete_selected_row (imap_dialog, &iter);
        }
    }
    g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (list);

    // Enable GUI refresh again
    gnc_resume_gui_refresh();

    // recount the number of invalid maps
    gtk_tree_model_foreach (imap_dialog->model,
                            (GtkTreeModelForeachFunc)find_invalid_mappings_total,
                            imap_dialog);

    if (imap_dialog->tot_invalid_maps == 0)
        gtk_widget_hide (imap_dialog->remove_button);

}

static gboolean
find_invalid_mappings (GtkTreeModel *model, GtkTreePath *path,
                         GtkTreeIter *iter, GList **rowref_list)
{
    Account *source_account = NULL;
    Account *map_account = NULL;
    gchar   *head;
    gint     depth;

    gtk_tree_model_get (model, iter, SOURCE_ACCOUNT, &source_account,
                                     MAP_ACCOUNT, &map_account,
                                     HEAD, &head, -1);

    depth = gtk_tree_path_get_depth (path);

    if ((source_account != NULL) && (map_account == NULL))
    {
        if (((g_strcmp0 (head, "online_id") == 0) && (depth == 1)) || (depth == 2))
        {
             GtkTreeRowReference *rowref = gtk_tree_row_reference_new (model, path);
             *rowref_list = g_list_prepend (*rowref_list, rowref);
        }
    }
    g_free (head);
    return FALSE;
}

static void
gnc_imap_remove_invalid_maps (ImapDialog *imap_dialog)
{
    GList *rr_list = NULL;
    GList *node;

    gtk_tree_model_foreach (imap_dialog->model,
                            (GtkTreeModelForeachFunc)find_invalid_mappings,
                            &rr_list);

    // Suspend GUI refreshing
    gnc_suspend_gui_refresh();

    // Walk the list
    for (node = rr_list; node != NULL; node = node->next)
    {
        GtkTreePath *path = gtk_tree_row_reference_get_path ((GtkTreeRowReference*)node->data);

        if (path)
        {
            GtkTreeIter iter;

            if (gtk_tree_model_get_iter (GTK_TREE_MODEL(imap_dialog->model), &iter, path))
                delete_selected_row (imap_dialog, &iter);

            gtk_tree_path_free (path);
        }
    }

    // Enable GUI refresh again
    gnc_resume_gui_refresh();

    g_list_foreach (rr_list, (GFunc)gtk_tree_row_reference_free, NULL);
    g_list_free (rr_list);
}

static void
gnc_imap_invalid_maps_dialog (ImapDialog *imap_dialog)
{
    gtk_widget_hide (imap_dialog->remove_button);

    if (imap_dialog->tot_invalid_maps > 0)
    {
        /* Translators: This is a ngettext(3) message, %d is the number of maps missing */
        gchar *message = g_strdup_printf (ngettext ("There is %d invalid mapping,\n\nWould you like to remove it now?",
                                                    "There are %d invalid mappings,\n\nWould you like to remove them now?",
                                                    imap_dialog->tot_invalid_maps),
                                                    imap_dialog->tot_invalid_maps);

        gchar *message2 = g_strdup_printf (gettext ("To see the invalid mappings, use a filter of '%s'"), _("Map Account NOT found"));

        gchar *text = g_strdup_printf ("%s\n\n%s\n\n%s", message, message2, _("(Note, if there is a large number, it may take a while)"));

        if (gnc_verify_dialog (GTK_WINDOW (imap_dialog->dialog), FALSE, "%s", text))
        {
            gnc_imap_remove_invalid_maps (imap_dialog);
            gtk_widget_hide (imap_dialog->remove_button);
        }
        else
        {
            gtk_widget_show (imap_dialog->remove_button);

            if (imap_dialog->type == BAYES)
                imap_dialog->inv_dialog_shown.inv_dialog_shown_bayes = TRUE;
            if (imap_dialog->type == NBAYES)
                imap_dialog->inv_dialog_shown.inv_dialog_shown_nbayes = TRUE;
            if (imap_dialog->type == ONLINE)
                imap_dialog->inv_dialog_shown.inv_dialog_shown_online = TRUE;
        }
        g_free (message);
        g_free (message2);
        g_free (text);
    }
}

static void
gnc_imap_invalid_maps (ImapDialog *imap_dialog)
{
    gboolean inv_dialog_shown = FALSE;

    if ((imap_dialog->type == BAYES) && (imap_dialog->inv_dialog_shown.inv_dialog_shown_bayes))
        inv_dialog_shown = TRUE;

    if ((imap_dialog->type == NBAYES) && (imap_dialog->inv_dialog_shown.inv_dialog_shown_nbayes))
        inv_dialog_shown = TRUE;

    if ((imap_dialog->type == ONLINE) && (imap_dialog->inv_dialog_shown.inv_dialog_shown_online))
        inv_dialog_shown = TRUE;

    if (!inv_dialog_shown)
        gnc_imap_invalid_maps_dialog (imap_dialog);
}

void
gnc_imap_dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
    ImapDialog *imap_dialog = user_data;

    switch (response_id)
    {
    case GTK_RESPONSE_APPLY:
        gnc_imap_dialog_delete (imap_dialog);
        return;

    case GTK_RESPONSE_REJECT:
        gnc_imap_invalid_maps_dialog (imap_dialog);
        return;

    case GTK_RESPONSE_CLOSE:
    default:
        gnc_close_gui_component_by_data (DIALOG_IMAP_CM_CLASS, imap_dialog);
        return;
    }
}

static gboolean
filter_test_and_move_next (ImapDialog *imap_dialog, GtkTreeIter *iter,
                           const gchar *filter_text)
{
    GtkTreePath *tree_path;
    gint         depth;
    gboolean     valid;
    gchar       *match_string;
    gchar       *map_full_acc;

    // Read the row
    gtk_tree_model_get (imap_dialog->model, iter, MATCH_STRING, &match_string, MAP_FULL_ACC, &map_full_acc, -1);

    // Get the level we are at in the tree-model
    tree_path = gtk_tree_model_get_path (imap_dialog->model, iter);
    depth = gtk_tree_path_get_depth (tree_path);

    // Reset filter to TRUE
    gtk_tree_store_set (GTK_TREE_STORE(imap_dialog->model), iter, FILTER, TRUE, -1);

    // Check for a filter_text entry
    if (filter_text && *filter_text != '\0')
    {
        if (match_string != NULL) // Check for match_string is not NULL, valid line
        {
            if ((g_strrstr (match_string, filter_text) == NULL) &&
                (g_strrstr (map_full_acc, filter_text) == NULL ))
                gtk_tree_store_set (GTK_TREE_STORE(imap_dialog->model), iter, FILTER, FALSE, -1);
            else
                gtk_tree_view_expand_to_path (GTK_TREE_VIEW(imap_dialog->view), tree_path);
        }
    }
    // Select next entry based on path
    if (depth == 1)
        gtk_tree_path_down (tree_path);
    else
    {
        gtk_tree_path_next (tree_path);
        if (!gtk_tree_model_get_iter (imap_dialog->model, iter, tree_path))
        {
            gtk_tree_path_prev (tree_path);
            gtk_tree_path_up (tree_path);
            gtk_tree_path_next (tree_path);
        }
    }
    valid = gtk_tree_model_get_iter (imap_dialog->model, iter, tree_path);

    gtk_tree_path_free (tree_path);
    g_free (match_string);
    g_free (map_full_acc);

    return valid;
}

static void
filter_button_cb (GtkButton *button, ImapDialog *imap_dialog)
{
    GtkTreeIter   iter;
    gboolean      valid;
    const gchar  *filter_text;

    filter_text = gtk_entry_get_text (GTK_ENTRY(imap_dialog->filter_text_entry));

    // Collapse all nodes
    gtk_tree_view_collapse_all (GTK_TREE_VIEW(imap_dialog->view));
    imap_dialog->apply_selection_filter = FALSE;

    // clear any selection
    gtk_tree_selection_unselect_all (gtk_tree_view_get_selection
                                    (GTK_TREE_VIEW(imap_dialog->view)));

    // do we have a filter, apply selection filter
    if (filter_text && *filter_text != '\0')
        imap_dialog->apply_selection_filter = TRUE;

    valid = gtk_tree_model_get_iter_first (imap_dialog->model, &iter);

    while (valid)
    {
        valid = filter_test_and_move_next (imap_dialog, &iter, filter_text);
    }
    gtk_widget_grab_focus (GTK_WIDGET(imap_dialog->view));
}

static void
expand_button_cb (GtkButton *button, ImapDialog *imap_dialog)
{
    // Clear the filter
    gtk_entry_set_text (GTK_ENTRY(imap_dialog->filter_text_entry), "");

    filter_button_cb (button, imap_dialog);

    gtk_tree_view_expand_all (GTK_TREE_VIEW(imap_dialog->view));
}

static void
collapse_button_cb (GtkButton *button, ImapDialog *imap_dialog)
{
    // Clear the filter
    gtk_entry_set_text (GTK_ENTRY(imap_dialog->filter_text_entry), "");

    filter_button_cb (button, imap_dialog);

    gtk_tree_view_collapse_all (GTK_TREE_VIEW(imap_dialog->view));
}

static void
list_type_selected_cb (GtkToggleButton* button, ImapDialog *imap_dialog)
{
    GncListType type;

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(imap_dialog->radio_bayes)))
        type = BAYES;
    else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(imap_dialog->radio_nbayes)))
        type = NBAYES;
    else
        type = ONLINE;

    if (type != ONLINE)
        gtk_widget_grab_focus (GTK_WIDGET(imap_dialog->filter_text_entry));

    // Lets do this only on change of list type
    if (type != imap_dialog->type)
    {
        gboolean inv_dialog_shown = FALSE;

        imap_dialog->type = type;
        get_account_info (imap_dialog);

        if ((imap_dialog->type == BAYES) && (imap_dialog->inv_dialog_shown.inv_dialog_shown_bayes))
            inv_dialog_shown = TRUE;

        if ((imap_dialog->type == NBAYES) && (imap_dialog->inv_dialog_shown.inv_dialog_shown_nbayes))
            inv_dialog_shown = TRUE;

        if ((imap_dialog->type == ONLINE) && (imap_dialog->inv_dialog_shown.inv_dialog_shown_online))
            inv_dialog_shown = TRUE;

        if (!inv_dialog_shown)
            gnc_imap_invalid_maps_dialog (imap_dialog);
    }
}

static void
show_count_column (ImapDialog *imap_dialog, gboolean show)
{
    GtkTreeViewColumn *tree_column;

    // Show Count Column
    tree_column = gtk_tree_view_get_column (GTK_TREE_VIEW(imap_dialog->view), 4);
    gtk_tree_view_column_set_visible (tree_column, show);

    // Hide Based on Column
    tree_column = gtk_tree_view_get_column (GTK_TREE_VIEW(imap_dialog->view), 1);
    gtk_tree_view_column_set_visible (tree_column, !show);

    gtk_tree_view_columns_autosize (GTK_TREE_VIEW(imap_dialog->view));
}

static void
add_to_store (ImapDialog *imap_dialog, GtkTreeIter *iter, const gchar *text, GncImapInfo *imapInfo)
{
    gchar       *fullname = NULL;
    gchar       *map_fullname = NULL;

    fullname = gnc_account_get_full_name (imapInfo->source_account);

    // Do we have a valid map account
    if (imapInfo->map_account == NULL)
    {
        // count the total invalid maps
        imap_dialog->tot_invalid_maps ++;

        map_fullname = g_strdup (_("Map Account NOT found"));
    }
    else
        map_fullname = gnc_account_get_full_name (imapInfo->map_account);

    // count the total entries
    imap_dialog->tot_entries ++;

    PINFO("Add to Store: Source Acc '%s', Head is '%s', Category is '%s', Match '%s', Map Acc '%s', Count is %s",
          fullname, imapInfo->head, imapInfo->category, imapInfo->match_string, map_fullname, imapInfo->count);

    gtk_tree_store_set (GTK_TREE_STORE(imap_dialog->model), iter,
                        SOURCE_FULL_ACC, fullname, SOURCE_ACCOUNT, imapInfo->source_account,
                        BASED_ON, text,
                        MATCH_STRING, imapInfo->match_string,
                        MAP_FULL_ACC, map_fullname, MAP_ACCOUNT, imapInfo->map_account,
                        HEAD, imapInfo->head, CATEGORY, imapInfo->category, COUNT, imapInfo->count,
                        FILTER, TRUE, -1);

    g_free (fullname);
    g_free (map_fullname);
}

static void
get_imap_info (ImapDialog *imap_dialog, Account *acc, const gchar *category, const gchar *text)
{
    GtkTreeIter  toplevel, child;
    GList *imap_list, *node;
    gchar *acc_name = NULL;
    gchar *head = NULL;

    acc_name = gnc_account_get_full_name (acc);
    PINFO("Source Acc '%s', Based on '%s', Path Head '%s'", acc_name, text, category);

    if (category == NULL) // For Bayesian, category is NULL
        imap_list = gnc_account_imap_get_info_bayes (acc);
    else
        imap_list = gnc_account_imap_get_info (acc, category);

    if (category == NULL)
        head = IMAP_FRAME_BAYES;
    else
        head = IMAP_FRAME;

    if (gnc_list_length_cmp (imap_list, 0))
    {
        PINFO("List length is %d", g_list_length (imap_list));

        // Add top level entry of Source full Account and Based on.
        gtk_tree_store_append (GTK_TREE_STORE(imap_dialog->model), &toplevel, NULL);
        gtk_tree_store_set (GTK_TREE_STORE(imap_dialog->model), &toplevel,
                        SOURCE_ACCOUNT, acc, SOURCE_FULL_ACC, acc_name,
                        HEAD, head, CATEGORY, category, BASED_ON, text, FILTER, TRUE, -1);

        for (node = imap_list;  node; node = g_list_next (node))
        {
            GncImapInfo *imapInfo = node->data;

            // First add a child entry and pass iter to add_to_store
            gtk_tree_store_append (GTK_TREE_STORE(imap_dialog->model), &child, &toplevel);
            add_to_store (imap_dialog, &child, text, imapInfo);

            // Free the members and structure
            g_free (imapInfo->head);
            g_free (imapInfo->category);
            g_free (imapInfo->match_string);
            g_free (imapInfo->count);
            g_free (imapInfo);
        }
    }
    g_free (acc_name);
    g_list_free (imap_list); // Free the List
}

static void
show_first_row (ImapDialog *imap_dialog)
{
    GtkTreeIter   iter;

    // See if there are any entries
    if (gtk_tree_model_get_iter_first (imap_dialog->model, &iter))
    {
        GtkTreePath *path;
        path = gtk_tree_path_new_first (); // Set Path to first entry
        gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW(imap_dialog->view), path, NULL, TRUE, 0.0, 0.0);
        gtk_tree_path_free (path);
    }
}

static void
get_account_info_bayes (ImapDialog *imap_dialog, GList *accts)
{
    GList   *ptr;

    /* Go through list of accounts */
    for (ptr = accts; ptr; ptr = g_list_next (ptr))
    {
        Account *acc = ptr->data;

        get_imap_info (imap_dialog, acc, NULL, _("Bayesian"));
    }
}

static void
get_account_info_nbayes (ImapDialog *imap_dialog, GList *accts)
{
    GList   *ptr;

    /* Go through list of accounts */
    for (ptr = accts; ptr; ptr = g_list_next (ptr))
    {
        Account *acc = ptr->data;

        // Description
        get_imap_info (imap_dialog, acc, IMAP_FRAME_DESC, _("Description Field"));

        // Memo
        get_imap_info (imap_dialog, acc, IMAP_FRAME_MEMO, _("Memo Field"));

        // CSV Account Map
        get_imap_info (imap_dialog, acc, IMAP_FRAME_CSV, _("CSV Account Map"));
    }
}

static void
get_account_info_online (ImapDialog *imap_dialog, GList *accts)
{
    GList       *ptr;
    GtkTreeIter  toplevel;

    GncImapInfo imapInfo;

    /* Go through list of accounts */
    for (ptr = accts; ptr; ptr = g_list_next (ptr))
    {
        gchar  *hbci_account_id = NULL;
        gchar  *hbci_bank_code = NULL;
        gchar  *text = NULL;
        Account *acc = ptr->data;

        // Check for online_id
        text = gnc_account_get_map_entry (acc, "online_id", NULL);

        if (text != NULL)
        {
            // Save source account
            imapInfo.source_account = acc;
            imapInfo.head = "online_id";
            imapInfo.category = " ";

            if (g_strcmp0 (text, "") == 0)
                imapInfo.map_account = NULL;
            else
                imapInfo.map_account = imapInfo.source_account;

            imapInfo.match_string = text;
            imapInfo.count = " ";

            // Add top level entry and pass iter to add_to_store
            gtk_tree_store_append (GTK_TREE_STORE(imap_dialog->model), &toplevel, NULL);
            add_to_store (imap_dialog, &toplevel, _("Online Id"), &imapInfo);
        }
        g_free (text);

        // Check for aqbanking hbci
        hbci_account_id = gnc_account_get_map_entry (acc, "hbci", "account-id");
        hbci_bank_code = gnc_account_get_map_entry (acc, "hbci", "bank-code");
        text = g_strconcat (hbci_bank_code, ",", hbci_account_id, NULL);

        if ((hbci_account_id != NULL) || (hbci_bank_code != NULL))
        {
            // Save source account
            imapInfo.source_account = acc;
            imapInfo.head = "hbci";
            imapInfo.category = " ";

            if (g_strcmp0 (text, "") == 0)
                imapInfo.map_account = NULL;
            else
                imapInfo.map_account = imapInfo.source_account;

            imapInfo.match_string = text;
            imapInfo.count = " ";

            // Add top level entry and pass iter to add_to_store
            gtk_tree_store_append (GTK_TREE_STORE(imap_dialog->model), &toplevel, NULL);
            add_to_store (imap_dialog, &toplevel, _("Online HBCI"), &imapInfo);
        }
        g_free (hbci_account_id);
        g_free (hbci_bank_code);
        g_free (text);
    }
}

static void
show_filter_option (ImapDialog *imap_dialog, gboolean show)
{
    if (show)
    {
        gtk_widget_show (imap_dialog->filter_text_entry);
        gtk_widget_show (imap_dialog->filter_button);
        gtk_widget_show (imap_dialog->filter_label);
        gtk_widget_show (imap_dialog->expand_button);
        gtk_widget_show (imap_dialog->collapse_button);
    }
    else
    {
        gtk_widget_hide (imap_dialog->filter_text_entry);
        gtk_widget_hide (imap_dialog->filter_button);
        gtk_widget_hide (imap_dialog->filter_label);
        gtk_widget_hide (imap_dialog->expand_button);
        gtk_widget_hide (imap_dialog->collapse_button);
    }
}

static void
get_account_info (ImapDialog *imap_dialog)
{
    Account      *root;
    GList        *accts;
    GtkTreeModel *fmodel;
    gchar        *total;

    /* Get list of Accounts */
    root = gnc_book_get_root_account (gnc_get_current_book());
    accts = gnc_account_get_descendants_sorted (root);

    imap_dialog->tot_entries = 0;
    imap_dialog->tot_invalid_maps = 0;

    fmodel = gtk_tree_view_get_model (GTK_TREE_VIEW(imap_dialog->view));

    imap_dialog->model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER(fmodel));

    // Disconnect the filter model from the treeview
    g_object_ref (G_OBJECT(imap_dialog->model));
    gtk_tree_view_set_model (GTK_TREE_VIEW(imap_dialog->view), NULL);

    // Clear the tree store
    gtk_tree_store_clear (GTK_TREE_STORE(imap_dialog->model));

    // Clear the filter
    gtk_entry_set_text (GTK_ENTRY(imap_dialog->filter_text_entry), "");
    imap_dialog->apply_selection_filter = FALSE;

    // Hide Count Column
    show_count_column (imap_dialog, FALSE);

    // Show Filter Option
    show_filter_option (imap_dialog, TRUE);

    if (imap_dialog->type == BAYES)
    {
        get_account_info_bayes (imap_dialog, accts);

        // Show Count Column
        show_count_column (imap_dialog, TRUE);
    }
    else if (imap_dialog->type == NBAYES)
        get_account_info_nbayes (imap_dialog, accts);
    else if (imap_dialog->type == ONLINE)
    {
        // Hide Filter Option
        show_filter_option (imap_dialog, FALSE);
        get_account_info_online (imap_dialog, accts);
    }
    // create a new filter model and reconnect to treeview
    fmodel = gtk_tree_model_filter_new (GTK_TREE_MODEL(imap_dialog->model), NULL);
    gtk_tree_model_filter_set_visible_column (GTK_TREE_MODEL_FILTER(fmodel), FILTER);
    g_object_unref (G_OBJECT(imap_dialog->model));

    gtk_tree_view_set_model (GTK_TREE_VIEW(imap_dialog->view), fmodel);
    g_object_unref (G_OBJECT(fmodel));

    // if there are any entries, show first row
    show_first_row (imap_dialog);

    // add the totals
    total = g_strdup_printf ("%s %d", _("Total Entries"), imap_dialog->tot_entries);
    gtk_label_set_text (GTK_LABEL(imap_dialog->total_entries_label), total);
    gtk_widget_show (imap_dialog->total_entries_label);
    g_free (total);

    if (imap_dialog->tot_invalid_maps > 0)
        gtk_widget_show (imap_dialog->remove_button);
    else
        gtk_widget_hide (imap_dialog->remove_button);

    g_list_free (accts);
}

static gboolean
view_selection_function (GtkTreeSelection *selection,
                         GtkTreeModel *model,
                         GtkTreePath *path,
                         gboolean path_currently_selected,
                         gpointer user_data)
{
    ImapDialog *imap_dialog = user_data;
    GtkTreeIter iter;

    if (!imap_dialog->apply_selection_filter)
        return TRUE;

    // do we have a valid row
    if (gtk_tree_model_get_iter (model, &iter, path))
    {
        gchar *match_string;

        // read the row
        gtk_tree_model_get (model, &iter, MATCH_STRING, &match_string, -1);

        // match_string NULL, top level can not be selected with a filter
        if (match_string == NULL)
            return FALSE;
        g_free (match_string);
    }
    return TRUE;
}

static void
gnc_imap_dialog_create (GtkWidget *parent, ImapDialog *imap_dialog)
{
    GtkWidget        *dialog;
    GtkBuilder       *builder;
    GtkTreeModel     *filter;
    GtkTreeSelection *selection;

    ENTER(" ");
    builder = gtk_builder_new();
    gnc_builder_add_from_file (builder, "dialog-imap-editor.glade", "tree-store");
    gnc_builder_add_from_file (builder, "dialog-imap-editor.glade", "treemodelfilter");
    gnc_builder_add_from_file (builder, "dialog-imap-editor.glade", "import_map_dialog");

    dialog = GTK_WIDGET(gtk_builder_get_object (builder, "import_map_dialog"));
    imap_dialog->dialog = dialog;

    // Set the name for this dialog so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(dialog), "gnc-id-import-map");

    imap_dialog->session = gnc_get_current_session();
    imap_dialog->type = BAYES;

    /* parent */
    if (parent != NULL)
        gtk_window_set_transient_for (GTK_WINDOW(dialog), GTK_WINDOW(parent));

    /* Connect the radio buttons...*/
    imap_dialog->radio_bayes = GTK_WIDGET(gtk_builder_get_object (builder, "radio-bayes"));
    imap_dialog->radio_nbayes = GTK_WIDGET(gtk_builder_get_object (builder, "radio-nbayes"));
    imap_dialog->radio_online = GTK_WIDGET(gtk_builder_get_object (builder, "radio-online"));
    g_signal_connect (imap_dialog->radio_bayes, "toggled",
                      G_CALLBACK(list_type_selected_cb), (gpointer)imap_dialog);
    g_signal_connect (imap_dialog->radio_nbayes, "toggled",
                      G_CALLBACK(list_type_selected_cb), (gpointer)imap_dialog);

    imap_dialog->total_entries_label = GTK_WIDGET(gtk_builder_get_object (builder, "total_entries_label"));
    imap_dialog->filter_text_entry = GTK_WIDGET(gtk_builder_get_object (builder, "filter-text-entry"));
    imap_dialog->filter_label = GTK_WIDGET(gtk_builder_get_object (builder, "filter-label"));
    imap_dialog->filter_button = GTK_WIDGET(gtk_builder_get_object (builder, "filter-button"));
    g_signal_connect (imap_dialog->filter_button, "clicked",
                      G_CALLBACK(filter_button_cb), (gpointer)imap_dialog);

    imap_dialog->expand_button = GTK_WIDGET(gtk_builder_get_object (builder, "expand-button"));
    g_signal_connect (imap_dialog->expand_button, "clicked",
                      G_CALLBACK(expand_button_cb), (gpointer)imap_dialog);

    imap_dialog->collapse_button = GTK_WIDGET(gtk_builder_get_object (builder, "collapse-button"));
    g_signal_connect (imap_dialog->collapse_button, "clicked",
                      G_CALLBACK(collapse_button_cb), (gpointer)imap_dialog);

    imap_dialog->view = GTK_WIDGET(gtk_builder_get_object (builder, "treeview"));

    imap_dialog->remove_button = GTK_WIDGET(gtk_builder_get_object (builder, "remove_button"));

    // Set filter column
    filter = gtk_tree_view_get_model (GTK_TREE_VIEW(imap_dialog->view));
    gtk_tree_model_filter_set_visible_column (GTK_TREE_MODEL_FILTER(filter), FILTER);

    // Set grid lines option to preference
    gtk_tree_view_set_grid_lines (GTK_TREE_VIEW(imap_dialog->view), gnc_tree_view_get_grid_lines_pref ());

    /* default to 'close' button */
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(imap_dialog->view));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

    /* add a select function  */
    gtk_tree_selection_set_select_function (selection,
                                            view_selection_function,
                                            imap_dialog,
                                            NULL);

    gtk_builder_connect_signals_full (builder, gnc_builder_connect_full_func, imap_dialog);

    g_object_unref (G_OBJECT(builder));

    gnc_restore_window_size (GNC_PREFS_GROUP, GTK_WINDOW(imap_dialog->dialog), GTK_WINDOW(parent));
    get_account_info (imap_dialog);

    LEAVE(" ");
}

static void
close_handler (gpointer user_data)
{
    ImapDialog *imap_dialog = user_data;

    ENTER(" ");
    gnc_save_window_size (GNC_PREFS_GROUP, GTK_WINDOW(imap_dialog->dialog));
    gtk_widget_destroy (GTK_WIDGET(imap_dialog->dialog));
    LEAVE(" ");
}

static void
refresh_handler (GHashTable *changes, gpointer user_data)
{
    ENTER(" ");
    LEAVE(" ");
}

static gboolean
show_handler (const char *klass, gint component_id,
              gpointer user_data, gpointer iter_data)
{
    ImapDialog *imap_dialog = user_data;

    ENTER(" ");
    if (!imap_dialog)
    {
        LEAVE("No data structure");
        return(FALSE);
    }
    gtk_window_present (GTK_WINDOW(imap_dialog->dialog));
    LEAVE(" ");
    return(TRUE);
}

/********************************************************************\
 * gnc_imap_dialog                                                  *
 * opens a window showing Bayesian and Non-Bayesian information     *
 *                                                                  *
 * Args:   parent  - the parent of the window to be created         *
 * Return: nothing                                                  *
\********************************************************************/
void
gnc_imap_dialog (GtkWidget *parent)
{
    ImapDialog *imap_dialog;
    gint component_id;

    ENTER(" ");
    if (gnc_forall_gui_components (DIALOG_IMAP_CM_CLASS, show_handler, NULL))
    {
        LEAVE("Existing dialog raised");
        return;
    }
    imap_dialog = g_new0 (ImapDialog, 1);

    gnc_imap_dialog_create (parent, imap_dialog);

    component_id = gnc_register_gui_component (DIALOG_IMAP_CM_CLASS,
                   refresh_handler, close_handler,
                   imap_dialog);

    gnc_gui_component_set_session (component_id, imap_dialog->session);

    gtk_widget_show (imap_dialog->dialog);
    gtk_widget_hide (imap_dialog->remove_button);
    gnc_imap_invalid_maps_dialog (imap_dialog);
    LEAVE(" ");
}
