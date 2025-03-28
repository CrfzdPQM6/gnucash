/********************************************************************\
 * dialog-sx-editor2.c : dialog for scheduled transaction editing   *
 * Copyright (C) 2001,2002,2006 Joshua Sled <jsled@asynchronous.org>*
 * Copyright (C) 2011 Robert Fewell                                 *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of version 2 and/or version 3 of the   *
 * GNU General Public License as published by the Free Software     *
 * Foundation.                                                      *
 *                                                                  *
 * As a special exception, permission is granted to link the binary *
 * module resultant from this code with the OpenSSL project's       *
 * "OpenSSL" library (or modified versions of it that use the same  *
 * license as the "OpenSSL" library), and distribute the linked     *
 * executable.  You must obey the GNU General Public License in all *
 * respects for all of the code used other than "OpenSSL". If you   *
 * modify this file, you may extend this exception to your version  *
 * of the file, but you are not obligated to do so. If you do not   *
 * wish to do so, delete this exception statement from your version *
 * of this file.                                                    *
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
#include <locale.h>

#include "qof.h"
#include "Account.h"
#include "SchedXaction.h"
#include "SX-book.h"
#include "dialog-preferences.h"
#include "dialog-sx-editor2.h"
#include "dialog-utils.h"
#include "gnc-component-manager.h"
#include "gnc-date.h"
#include "gnc-date-edit.h"
#include "gnc-dense-cal.h"
#include "gnc-dense-cal-store.h"
#include "gnc-embedded-window.h"
#include "gnc-engine.h"
#include "gnc-frequency.h"
#include "gnc-gui-query.h"
#include "gnc-hooks.h"
#include "gnc-ledger-display.h"
#include "gnc-plugin-page.h"
#include "gnc-plugin-page-register2.h"
#include "gnc-prefs.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include "gnc-tree-model-split-reg.h"
#include "gnc-tree-control-split-reg.h"
#include <gnc-glib-utils.h>

#include "gnc-sx-instance-model.h"
#include "dialog-sx-since-last-run.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnc.gui.sx.editor"

static QofLogModule log_module = GNC_MOD_GUI_SX;

static gint _sx_engine_event_handler_id = -1;

#define END_NEVER_OPTION 0
#define END_DATE_OPTION  1
#define NUM_OCCUR_OPTION 2

#define NUM_LEDGER_LINES_DEFAULT 6

#define EX_CAL_NUM_MONTHS 6
#define EX_CAL_MO_PER_COL 3

#define GNC_D_WIDTH 25
#define GNC_D_BUF_WIDTH 26

/** Datatypes ***********************************************************/

typedef enum _EndTypeEnum
{
    END_NEVER,
    END_DATE,
    END_OCCUR,
} EndType;

typedef enum { NO_END, DATE_END, COUNT_END } END_TYPE;

struct _GncSxEditorDialog2
{
    GtkWidget    *dialog;
    GtkBuilder   *builder;
    GtkNotebook  *notebook;
    SchedXaction *sx;
    /* If this is a new scheduled transaction or not. */
    int newsxP;

    /* The various widgets in the dialog */
    GNCLedgerDisplay2 *ledger;

    GncFrequency     *gncfreq;
    GncDenseCalStore *dense_cal_model;
    GncDenseCal      *example_cal;

    GtkEditable *nameEntry;

    GtkLabel *lastOccurLabel;

    GtkToggleButton *enabledOpt;
    GtkToggleButton *autocreateOpt;
    GtkToggleButton *notifyOpt;
    GtkToggleButton *advanceOpt;
    GtkSpinButton   *advanceSpin;
    GtkToggleButton *remindOpt;
    GtkSpinButton   *remindSpin;

    GtkToggleButton *optEndDate;
    GtkToggleButton *optEndNone;
    GtkToggleButton *optEndCount;
    GtkEntry        *endCountSpin;
    GtkEntry        *endRemainSpin;
    GNCDateEdit     *endDateEntry;

    char *sxGUIDstr;

    GncEmbeddedWindow *embed_window;
    GncPluginPage     *plugin_page;
};

/** Prototypes **********************************************************/

static void schedXact_editor_create_freq_sel (GncSxEditorDialog2 *sxed);
static void schedXact_editor_create_ledger (GncSxEditorDialog2 *sxed);
static void schedXact_editor_populate (GncSxEditorDialog2 *);
static void endgroup_rb_toggled_cb (GtkButton *b, gpointer d);
static void set_endgroup_toggle_states (GncSxEditorDialog2 *sxed, EndType t);
static void advance_toggled_cb (GtkButton *b, GncSxEditorDialog2 *sxed);
static void remind_toggled_cb (GtkButton *b, GncSxEditorDialog2 *sxed);
static gboolean gnc_sxed_check_consistent (GncSxEditorDialog2 *sxed);
static gboolean gnc_sxed_check_changed (GncSxEditorDialog2 *sxed);
static void gnc_sxed_save_sx (GncSxEditorDialog2 *sxed);
static void gnc_sxed_freq_changed (GncFrequency *gf, gpointer ud);
static void sxed_excal_update_adapt_cb (GtkWidget *o, gpointer ud);
static void gnc_sxed_update_cal (GncSxEditorDialog2 *sxed);
static void on_sx_check_toggled_cb (GtkWidget *togglebutton, gpointer user_data);
static void gnc_sxed_reg_check_close (GncSxEditorDialog2 *sxed);
static gboolean sxed_delete_event (GtkWidget *widget, GdkEvent *event, gpointer ud);
static gboolean sxed_confirmed_cancel (GncSxEditorDialog2 *sxed);
static gboolean editor_component_sx_equality (gpointer find_data, gpointer user_data);

static GtkActionEntry gnc_sxed_menu_entries [] =
{
    { "EditAction", NULL, N_("_Edit"), NULL, NULL, NULL },
    { "TransactionAction", NULL, N_("_Transaction"), NULL, NULL, NULL },
    { "ViewAction", NULL, N_("_View"), NULL, NULL, NULL },
    { "ActionsAction", NULL, N_("_Actions"), NULL, NULL, NULL },
};
static guint gnc_sxed_menu_n_entries = G_N_ELEMENTS (gnc_sxed_menu_entries);

/** Implementations *****************************************************/

static void
sxed_close_handler(gpointer user_data)
{
    GncSxEditorDialog2 *sxed = user_data;

    gnc_sxed_reg_check_close(sxed);
    gnc_save_window_size( GNC_PREFS_GROUP_SXED, GTK_WINDOW(sxed->dialog) );
    gtk_widget_destroy(sxed->dialog);
    /* The data will be cleaned up in the destroy handler. */
}


/**
 * @return TRUE if the user does want to cancel, FALSE if not.  If TRUE is
 * returned, the register's changes have been cancelled.
 **/
static gboolean
sxed_confirmed_cancel (GncSxEditorDialog2 *sxed)
{
    GncTreeViewSplitReg *view;

    view = gnc_ledger_display2_get_split_view_register (sxed->ledger);
    /* check for changes */
    if (gnc_sxed_check_changed (sxed))
    {
        const char *sx_changed_msg =
            _( "This Scheduled Transaction has changed; are you "
               "sure you want to cancel?" );
        if (!gnc_verify_dialog (GTK_WINDOW (sxed->dialog), FALSE, "%s", sx_changed_msg))
        {
            return FALSE;
        }
    }
    /* cancel ledger changes */
    gnc_tree_view_split_reg_cancel_edit (view, TRUE);
    return TRUE;
}


/**********************************
 * Dialog Action Button functions *
 *********************************/
static void
editor_cancel_button_clicked_cb (GtkButton *b, GncSxEditorDialog2 *sxed)
{
    /* close */
    if (!sxed_confirmed_cancel (sxed))
        return;

    gnc_close_gui_component_by_data (DIALOG_SCHEDXACTION2_EDITOR_CM_CLASS,
                                     sxed);
}


static void
editor_help_button_clicked_cb (GtkButton *b, GncSxEditorDialog2 *sxed)
{
    gnc_gnome_help (GTK_WINDOW(sxed->dialog), HF_HELP, HL_SXEDITOR);
}


static void
editor_ok_button_clicked_cb (GtkButton *b, GncSxEditorDialog2 *sxed)
{
    QofBook *book;
    SchedXactions *sxes;

    if (!gnc_sxed_check_consistent (sxed))
        return;

    gnc_sxed_save_sx (sxed);

    /* add to list */
    // @@fixme -- forget 'new'-flag: check for existence of the SX [?]
    if ( sxed->newsxP )
    {
        book = gnc_get_current_book ();
        sxes = gnc_book_get_schedxactions (book);
        gnc_sxes_add_sx (sxes, sxed->sx);
        sxed->newsxP = FALSE;
    }

    /* cleanup */
    gnc_close_gui_component_by_data (DIALOG_SCHEDXACTION2_EDITOR_CM_CLASS,
                                     sxed);
}


/*************************************************************************
 * Checks to see if the SX has been modified from it's previously-saved
 * state.
 * @return TRUE if this is a 'new' SX, or if the SX has changed from it's
 *   previous configuration.
 ************************************************************************/
static gboolean
gnc_sxed_check_changed (GncSxEditorDialog2 *sxed)
{
    if (sxed->newsxP)
        return TRUE;

    /* name */
    {
        char *name;

        name = gtk_editable_get_chars (GTK_EDITABLE (sxed->nameEntry), 0, -1);
        if (strlen (name) == 0)
        {
            return TRUE;

        }
        if ((xaccSchedXactionGetName (sxed->sx) == NULL)
                || (strcmp( xaccSchedXactionGetName (sxed->sx),
                            name ) != 0))
        {
            return TRUE;
        }
    }

    /* end options */
    {
        /* dialog says... no end */
        if (gtk_toggle_button_get_active (sxed->optEndNone))
        {
            if (xaccSchedXactionHasEndDate (sxed->sx)
                    || xaccSchedXactionHasOccurDef (sxed->sx))
            {
                return TRUE;
            }
        }

        /* dialog says... end date */
        if (gtk_toggle_button_get_active (sxed->optEndDate))
        {
            GDate sxEndDate, dlgEndDate;

            if (!xaccSchedXactionHasEndDate (sxed->sx))
            {
                return TRUE;
            }
            sxEndDate = *xaccSchedXactionGetEndDate (sxed->sx);
            gnc_gdate_set_time64 (&dlgEndDate,
                               gnc_date_edit_get_date( sxed->
                                       endDateEntry));

            if (g_date_compare (&sxEndDate, &dlgEndDate) != 0)
            {
                return TRUE;
            }
        }

        /* dialog says... num occur */
        if (gtk_toggle_button_get_active (sxed->optEndCount))
        {
            gint sxNumOccur, sxNumRem, dlgNumOccur, dlgNumRem;

            if (!xaccSchedXactionGetNumOccur (sxed->sx))
            {
                return TRUE;
            }

            dlgNumOccur  =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endCountSpin));

            dlgNumRem =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endRemainSpin));

            sxNumOccur = xaccSchedXactionGetNumOccur (sxed->sx);
            sxNumRem = xaccSchedXactionGetRemOccur (sxed->sx);

            if ((dlgNumOccur != sxNumOccur)
                    || (dlgNumRem != sxNumRem))
            {
                return TRUE;
            }
        }
    }

    /* SX options [autocreate, notify, reminder, advance] */
    {
        gboolean dlgEnabled,
                 dlgAutoCreate,
                 dlgNotify,
                 sxEnabled,
                 sxAutoCreate,
                 sxNotify;
        gint dlgAdvance, sxAdvance;
        gint dlgRemind, sxRemind;

        dlgEnabled =
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->
                                          enabledOpt));
        dlgAutoCreate =
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->
                                          autocreateOpt));
        dlgNotify =
            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->
                                          notifyOpt));

        sxEnabled = xaccSchedXactionGetEnabled (sxed->sx);
        if (!((dlgEnabled == sxEnabled)))
        {
            return TRUE;
        }

        xaccSchedXactionGetAutoCreate (sxed->sx, &sxAutoCreate, &sxNotify);
        if (!((dlgAutoCreate == sxAutoCreate)
                && (dlgNotify == sxNotify)))
        {
            return TRUE;
        }

        dlgAdvance = 0;
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->advanceOpt)))
        {
            dlgAdvance =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->
                                                  advanceSpin));
        }
        sxAdvance = xaccSchedXactionGetAdvanceCreation (sxed->sx);
        if (dlgAdvance != sxAdvance)
        {
            return TRUE;
        }

        dlgRemind = 0;
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->remindOpt)))
        {
            dlgRemind =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->remindSpin));
        }
        sxRemind = xaccSchedXactionGetAdvanceReminder (sxed->sx);
        if (dlgRemind != sxRemind)
        {
            return TRUE;
        }
    }

    {
        GList *dialog_schedule = NULL;
        GDate dialog_start_date, sx_start_date;
        gchar *dialog_schedule_str, *sx_schedule_str;
        gboolean schedules_are_the_same, start_dates_are_the_same;

        g_date_clear (&dialog_start_date, 1);
        gnc_frequency_save_to_recurrence (sxed->gncfreq, &dialog_schedule, &dialog_start_date);
        dialog_schedule_str = recurrenceListToString (dialog_schedule);
        recurrenceListFree (&dialog_schedule);

        sx_start_date = *xaccSchedXactionGetStartDate (sxed->sx);
        sx_schedule_str = recurrenceListToString (gnc_sx_get_schedule (sxed->sx));

        DEBUG ("dialog schedule [%s], sx schedule [%s]",
                dialog_schedule_str, sx_schedule_str);

        schedules_are_the_same = (strcmp (dialog_schedule_str, sx_schedule_str) == 0);
        g_free (dialog_schedule_str);
        g_free (sx_schedule_str);

        start_dates_are_the_same = (g_date_compare (&dialog_start_date, &sx_start_date) == 0);

        if (!schedules_are_the_same || !start_dates_are_the_same)
            return TRUE;
    }

    /* template transactions */
    {
        GncTreeViewSplitReg *view = gnc_ledger_display2_get_split_view_register (sxed->ledger);
        /* Make sure we have finished the edit */
        gnc_tree_view_split_reg_finish_edit (view);
        if (gnc_tree_view_split_reg_get_dirty_trans (view) != NULL)
            return TRUE;
    }
    return FALSE;
}


/*****************************************************************************
 * Holds the credit- and debit-sum for a given Transaction, as used in
 * gnc_sxed_check_consistent.
 ****************************************************************************/
typedef struct _txnCreditDebitSums
{
    gnc_numeric creditSum;
    gnc_numeric debitSum;
} txnCreditDebitSums;


static
void
set_sums_to_zero (gpointer key,
                  gpointer val,
                  gpointer ud)
{
    txnCreditDebitSums *tcds = (txnCreditDebitSums*)val;
    tcds->creditSum = gnc_numeric_zero();
    tcds->debitSum  = gnc_numeric_zero();
}


static void
check_credit_debit_balance (gpointer key,
                            gpointer val,
                            gpointer ud)
{
    txnCreditDebitSums *tcds = (txnCreditDebitSums*)val;
    gboolean *unbalanced = (gboolean*)ud;
    *unbalanced |= !(gnc_numeric_zero_p (
                         gnc_numeric_sub_fixed (tcds->debitSum,
                                 tcds->creditSum)));

    if (qof_log_check (G_LOG_DOMAIN, QOF_LOG_DEBUG))
    {
        if (gnc_numeric_zero_p (gnc_numeric_sub_fixed (tcds->debitSum,
                                 tcds->creditSum)))
        {
            DEBUG ("%p | true [%s - %s = %s]",
                     key,
                     gnc_numeric_to_string (tcds->debitSum),
                     gnc_numeric_to_string (tcds->creditSum),
                     gnc_numeric_to_string (gnc_numeric_sub_fixed (tcds->debitSum,
                                           tcds->creditSum)));
        }
        else
        {
            DEBUG ("%p | false [%s - %s = %s]",
                     key,
                     gnc_numeric_to_string (tcds->debitSum),
                     gnc_numeric_to_string (tcds->creditSum),
                     gnc_numeric_to_string (gnc_numeric_sub_fixed (tcds->debitSum,
                                           tcds->creditSum)));
        }
    }
}


/*******************************************************************************
 * Checks to make sure that the SX is in a reasonable state to save.
 * @return true if checks out okay, false otherwise.
 ******************************************************************************/
static gboolean
gnc_sxed_check_consistent (GncSxEditorDialog2 *sxed)
{
    gboolean multi_commodity = FALSE;
    gnc_commodity *base_cmdty = NULL;
    gint ttVarCount, splitCount = 0;
    GList *schedule = NULL;

    /* Do checks on validity and such, interrupting the user if
     * things aren't right.
     *
     * Features...
     * X support formulas [?!]
     * X balancing the SX if contain numeric-only formula data.
     *   X agreement with create-automagically/notification controls
     * X the 'will ever be valid' check should take num-occur vals into
     *   account.
     * X SX name is unique
     * X SX has a name
     * X "weekly" FS has some days set.
     * X "once" with reasonable start/end dates.
     *   X This doesn't work at the time the 'weekly' one was fixed with
     *     user-confirmation, below; the once SX is always valid.
     * [X more generically, creating a "not scheduled" SX is probably not
     *   right... ]
     */

    {
        static const int NUM_ITERS_WITH_VARS = 5;
        static const int NUM_ITERS_NO_VARS = 1;
        int numIters, i;
        GHashTable *vars, *txns;
        GList *splitList = NULL;
        char *credit_formula = NULL, *debit_formula = NULL;
        Split *s;
        Transaction *t;
        gnc_numeric tmp;
        gboolean unbalanceable;
        gpointer unusedKey, unusedValue;

        unbalanceable = FALSE; /* innocent until proven guilty */
        vars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)gnc_sx_variable_free);
        txns = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
        numIters = NUM_ITERS_NO_VARS;
        /**
         * Plan:
         * . Do a first pass to get the variables.
         * . Set each variable to random values.
         * . see if we balance after that
         *   . true: all good
         *   . false: indicate to user, allow decision.
         */

        /* numeric-formulas-get-balanced determination */
        gnc_sx_get_variables (sxed->sx, vars);

        ttVarCount = g_hash_table_size (vars);
        if (ttVarCount != 0)
        {
            /* balance with random variable bindings some number
             * of times in an attempt to ferret out
             * un-balanceable transactions.
             *
             * NOTE: The Real Way to do this is with some
             * symbolic math to eliminate the variables.  This is
             * hard, and we don't do it.  This solution will
             * suffice for now, and perhaps for the lifetime of
             * the software. --jsled */
            numIters = NUM_ITERS_WITH_VARS;
        }

        for (i = 0; i < numIters && !unbalanceable; i++)
        {
            gnc_sx_randomize_variables (vars);
            g_hash_table_foreach (txns, set_sums_to_zero, NULL);
            tmp = gnc_numeric_zero();

            splitList = xaccSchedXactionGetSplits (sxed->sx);
            splitCount += g_list_length (splitList);

            for (; splitList; splitList = splitList->next)
            {
                GncGUID *acct_guid = NULL;
                Account *acct;
                gnc_commodity *split_cmdty;
                txnCreditDebitSums *tcds;

                s = (Split*)splitList->data;
                t = xaccSplitGetParent (s);

                if (!(tcds =
                            (txnCreditDebitSums*)g_hash_table_lookup (txns,
                                    (gpointer)t)))
                {
                    tcds = g_new0 (txnCreditDebitSums, 1 );
                    tcds->creditSum = gnc_numeric_zero();
                    tcds->debitSum  = gnc_numeric_zero();
                    g_hash_table_insert (txns, (gpointer)t, (gpointer)tcds);
                }

		qof_instance_get (QOF_INSTANCE (s),
				  "sx-account", &acct_guid,
				  "sx-credit-formula", &credit_formula,
				  "sx-debit-formula", &debit_formula,
				  NULL);
                acct = xaccAccountLookup( acct_guid, gnc_get_current_book ());
                guid_free (acct_guid);
                split_cmdty = xaccAccountGetCommodity(acct);
                if (base_cmdty == NULL)
                {
                    base_cmdty = split_cmdty;
                }
                multi_commodity |= !gnc_commodity_equal(split_cmdty, base_cmdty);

		if ( credit_formula &&
		     g_strcmp0 (credit_formula, "") != 0 &&
		     gnc_sx_parse_vars_from_formula(credit_formula, vars,
						    &tmp ) < 0 )
		{
		    GString *errStr;

		    errStr = g_string_sized_new( 32 );
		    g_string_printf( errStr,
				     _( "Couldn't parse credit formula for "
					"split \"%s\"." ),
				     xaccSplitGetMemo( s ) );
                    gnc_error_dialog (GTK_WINDOW (sxed->dialog), "%s",
				      errStr->str );
		    g_string_free( errStr, TRUE );

		    return FALSE;
		}
		tcds->creditSum =
		    gnc_numeric_add( tcds->creditSum, tmp, 100,
				     (GNC_DENOM_AUTO | GNC_HOW_DENOM_LCD) );
		tmp = gnc_numeric_zero();
		if ( debit_formula &&
		     g_strcmp0 (debit_formula, "") != 0 &&
		     gnc_sx_parse_vars_from_formula( debit_formula, vars,
						     &tmp ) < 0 )
		{
		    GString *errStr;

		    errStr = g_string_sized_new( 32 );
		    g_string_printf( errStr,
				     _( "Couldn't parse debit formula for "
					"split \"%s\"." ),
				     xaccSplitGetMemo( s ) );
                    gnc_error_dialog (GTK_WINDOW (sxed->dialog), "%s",
				      (gchar*)errStr->str );
		    g_string_free( errStr, TRUE );

		    return FALSE;
		}
		tcds->debitSum = gnc_numeric_add( tcds->debitSum, tmp, 100,
						  (GNC_DENOM_AUTO | GNC_HOW_DENOM_LCD) );
		tmp = gnc_numeric_zero();
            }

            g_hash_table_foreach (txns,
                                  check_credit_debit_balance,
                                  &unbalanceable);
        }

        /* Subtract out pre-defined vars */
        if (g_hash_table_lookup_extended (vars, "i",
                                         &unusedKey,
                                         &unusedValue))
        {
            ttVarCount -= 1;
        }

        g_hash_table_destroy (vars);
        g_hash_table_destroy (txns);

        if (unbalanceable
                && !gnc_verify_dialog (GTK_WINDOW (sxed->dialog), FALSE,
                                       "%s",
                                       _("The Scheduled Transaction Editor "
                                         "cannot automatically balance "
                                         "this transaction. "
                                         "Should it still be "
                                         "entered?")))
        {
            return FALSE;
        }
    }

    /* read out data back into SchedXaction object. */
    /* FIXME: this is getting too deep; split out. */
    {
        gchar *name, *nameKey;
        gboolean nameExists, nameHasChanged;
        GList *sxList;

        name = gtk_editable_get_chars (GTK_EDITABLE (sxed->nameEntry), 0, -1);
        if (strlen (name) == 0 )
        {
            const char *sx_has_no_name_msg =
                _("Please name the Scheduled Transaction.");
            gnc_error_dialog (GTK_WINDOW (sxed->dialog), "%s", sx_has_no_name_msg);
            g_free (name);
            return FALSE;

        }

        nameExists = FALSE;
        nameKey = g_utf8_collate_key (name, -1);
        nameHasChanged =
            (xaccSchedXactionGetName (sxed->sx) == NULL)
            || (strcmp (xaccSchedXactionGetName (sxed->sx), name) != 0);
        for ( sxList =
                    gnc_book_get_schedxactions (gnc_get_current_book())->sx_list;
                nameHasChanged && !nameExists && sxList;
                sxList = sxList->next )
        {
            char *existingName, *existingNameKey;
            existingName =
                xaccSchedXactionGetName ((SchedXaction*)sxList->data);
            existingNameKey = g_utf8_collate_key (existingName, -1);
            nameExists |= (strcmp (nameKey, existingNameKey) == 0);
            g_free (existingNameKey);
        }
        if (nameHasChanged && nameExists)
        {
            const char *sx_has_existing_name_msg =
                _("A Scheduled Transaction with the "
                   "name \"%s\" already exists. "
                   "Are you sure you want to name "
                   "this one the same?");
            if (!gnc_verify_dialog (GTK_WINDOW (sxed->dialog), FALSE,
                                      sx_has_existing_name_msg,
                                      name))
            {
                g_free (nameKey);
                g_free (name);
                return FALSE;
            }
        }
        g_free (nameKey);
        g_free (name);
    }

    // @@FIXME: similar to below, check the commodities involved, and disallow autocreation
    {
        gboolean autocreateState;

        autocreateState =
            gtk_toggle_button_get_active (
                GTK_TOGGLE_BUTTON (sxed->autocreateOpt));

        if (((ttVarCount > 0) || multi_commodity) && autocreateState)
        {
            gnc_warning_dialog (GTK_WINDOW (sxed->dialog), "%s",
                               _("Scheduled Transactions with variables "
                                 "cannot be automatically created."));
            return FALSE;
        }

        /* Fix for part of Bug#121740 -- auto-create transactions are
         * only valid if there's actually a transaction to create. */
        if (autocreateState && splitCount == 0)
        {
            gnc_warning_dialog (GTK_WINDOW (sxed->dialog), "%s",
                               _("Scheduled Transactions without a template "
                                 "transaction cannot be automatically created."));
            return FALSE;
        }
    }

    /* deal with time. */
    {
        GDate startDate, endDate, nextDate;

        if (!gtk_toggle_button_get_active (sxed->optEndDate)
                && !gtk_toggle_button_get_active (sxed->optEndCount)
                && !gtk_toggle_button_get_active (sxed->optEndNone) )
        {
            const char *sx_end_spec_msg =
                _("Please provide a valid end selection.");
            gnc_error_dialog (GTK_WINDOW (sxed->dialog), "%s", sx_end_spec_msg);
            return FALSE;
        }

        if (gtk_toggle_button_get_active (sxed->optEndCount))
        {
            gint occur, rem;

            occur  =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endCountSpin));

            rem =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endRemainSpin));

            if (occur == 0)
            {
                const char *sx_occur_count_zero_msg =
                    _("There must be some number of occurrences.");
                gnc_error_dialog (GTK_WINDOW (sxed->dialog), "%s",
                                  sx_occur_count_zero_msg);
                return FALSE;
            }

            if (rem > occur)
            {
                const char *sx_occur_counts_wrong_msg =
                    _("The number of remaining occurrences "
                       "(%d) is greater than the number of "
                       "total occurrences (%d).");
                gnc_error_dialog (GTK_WINDOW (sxed->dialog),
                                  sx_occur_counts_wrong_msg,
                                  rem, occur);
                return FALSE;
            }

        }

        g_date_clear (&endDate, 1);
        if ( gtk_toggle_button_get_active(sxed->optEndDate))
        {
            gnc_gdate_set_time64 (&endDate,
                               gnc_date_edit_get_date (sxed->
                                       endDateEntry));
        }

        g_date_clear (&nextDate, 1);
        gnc_frequency_save_to_recurrence (sxed->gncfreq, &schedule, &startDate);
        if (gnc_list_length_cmp (schedule, 0))
        {
            g_date_subtract_days (&startDate, 1);
            recurrenceListNextInstance (schedule, &startDate, &nextDate);
        }
        recurrenceListFree (&schedule);

        if (!g_date_valid (&nextDate)
                || (g_date_valid (&endDate) && (g_date_compare (&nextDate, &endDate) > 0)))
        {
            const char *invalid_sx_check_msg =
                _("You have attempted to create a Scheduled "
                  "Transaction which will never run. Do you "
                  "really want to do this?");
            if (!gnc_verify_dialog (GTK_WINDOW (sxed->dialog), FALSE,
                                   "%s", invalid_sx_check_msg))
                return FALSE;
        }
    }
    return TRUE;
}


/******************************************************************************
 * Saves the contents of the SX.  This assumes that gnc_sxed_check_consistent
 * has returned true.
 *****************************************************************************/
static void
gnc_sxed_save_sx (GncSxEditorDialog2 *sxed )
{
    gnc_sx_begin_edit (sxed->sx);

    /* name */
    {
        char *name;

        name = gtk_editable_get_chars (sxed->nameEntry, 0, -1);
        xaccSchedXactionSetName (sxed->sx, name);
        g_free (name);
    }

    /* date */
    {
        GDate gdate;

        if (gtk_toggle_button_get_active (sxed->optEndDate))
        {
            /* get the end date data */
            gnc_gdate_set_time64 (&gdate,
                               gnc_date_edit_get_date (
                                   sxed->endDateEntry));
            xaccSchedXactionSetEndDate (sxed->sx, &gdate);
            /* set the num occurrences data */
            xaccSchedXactionSetNumOccur (sxed->sx, 0);
        }
        else if (gtk_toggle_button_get_active (sxed->optEndCount))
        {
            gint num;

            /* get the occurrences data */
            num  =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endCountSpin));
            xaccSchedXactionSetNumOccur (sxed->sx, num);

            num =
                gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endRemainSpin));
            xaccSchedXactionSetRemOccur (sxed->sx, num );

            g_date_clear (&gdate, 1);
            xaccSchedXactionSetEndDate (sxed->sx, &gdate);
        }
        else if (gtk_toggle_button_get_active (sxed->optEndNone))
        {
            xaccSchedXactionSetNumOccur (sxed->sx, 0);
            g_date_clear (&gdate, 1);
            xaccSchedXactionSetEndDate (sxed->sx, &gdate);
        }
        else
        {
            g_critical ("no valid end specified\n");
        }
    }

    /* Enabled states */
    {
        gboolean enabledState;

        enabledState = gtk_toggle_button_get_active (sxed->enabledOpt);
        xaccSchedXactionSetEnabled (sxed->sx, enabledState);
    }

    /* Auto-create/notification states */
    {
        gboolean autocreateState, notifyState;

        autocreateState = gtk_toggle_button_get_active (sxed->autocreateOpt);
        notifyState = gtk_toggle_button_get_active (sxed->notifyOpt);
        /* "Notify" only makes sense if AutoCreate is activated;
         * enforce that here. */
        xaccSchedXactionSetAutoCreate (sxed->sx,
                                       autocreateState,
                                       (autocreateState & notifyState));
    }

    /* days in advance */
    {
        int daysInAdvance;

        daysInAdvance = 0;
        if (gtk_toggle_button_get_active (sxed->advanceOpt))
        {
            daysInAdvance =
                gtk_spin_button_get_value_as_int (sxed->advanceSpin);
        }
        xaccSchedXactionSetAdvanceCreation (sxed->sx, daysInAdvance);

        daysInAdvance = 0;
        if (gtk_toggle_button_get_active (sxed->remindOpt))
        {
            daysInAdvance =
                gtk_spin_button_get_value_as_int (sxed->remindSpin);
        }
        xaccSchedXactionSetAdvanceReminder (sxed->sx, daysInAdvance);
    }

    /* start date and freq spec */
    {
        GDate gdate;
        GList *schedule = NULL;

        gnc_frequency_save_to_recurrence (sxed->gncfreq, &schedule, &gdate);
        gnc_sx_set_schedule (sxed->sx, schedule);
        {
            gchar *recurrence_str = recurrenceListToCompactString (schedule);
            DEBUG("recurrences parsed [%s]", recurrence_str);
            g_free (recurrence_str);
        }

        /* now that we have it, set the start date */
        xaccSchedXactionSetStartDate (sxed->sx, &gdate);
    }

    gnc_sx_commit_edit (sxed->sx);
}


static void
enabled_toggled_cb (GtkToggleButton *o, GncSxEditorDialog2 *sxed)
{
    return;
}


static void
autocreate_toggled_cb (GtkToggleButton *o, GncSxEditorDialog2 *sxed)
{
    if ( !gtk_toggle_button_get_active (o))
    {
        gtk_toggle_button_set_active (sxed->notifyOpt, FALSE);
    }
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->notifyOpt),
                              gtk_toggle_button_get_active (o));
}


static void
advance_toggled_cb (GtkButton *o, GncSxEditorDialog2 *sxed)
{

    gtk_widget_set_sensitive(GTK_WIDGET (sxed->advanceSpin),
                              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->advanceOpt)));
    gtk_editable_set_editable(GTK_EDITABLE (sxed->advanceSpin), TRUE);
}


static void
remind_toggled_cb (GtkButton *o, GncSxEditorDialog2 *sxed)
{

    gtk_widget_set_sensitive (GTK_WIDGET (sxed->remindSpin),
                              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sxed->remindOpt)));
    gtk_editable_set_editable (GTK_EDITABLE (sxed->remindSpin), TRUE);
}


/* Local destruction of dialog */
static void
scheduledxaction_editor_dialog_destroy (GtkWidget *object, gpointer data)
{
    GncSxEditorDialog2 *sxed = data;

    if (sxed == NULL)
        return;

    gnc_unregister_gui_component_by_data
    (DIALOG_SCHEDXACTION2_EDITOR_CM_CLASS, sxed);

    gnc_embedded_window_close_page (sxed->embed_window, sxed->plugin_page);
    gtk_widget_destroy (GTK_WIDGET (sxed->embed_window));
    sxed->embed_window = NULL;
    sxed->plugin_page = NULL;
    sxed->ledger = NULL;

    g_free (sxed->sxGUIDstr);
    sxed->sxGUIDstr = NULL;

    if ( sxed->newsxP )
    {
        /* FIXME: WTF???
         *
         * "WTF" explanation: in the "new" click from the caller, we
         * set this flag.  When "ok" is pressed on the dialog, we set
         * this flag to false, and thus leave the SX live.  If
         * "Cancel" is clicked, the flag will still be true, and this
         * SX will be cleaned, here. -- jsled
         */
        gnc_sx_begin_edit (sxed->sx);
        xaccSchedXactionDestroy (sxed->sx);
    }
    sxed->sx = NULL;

    g_free (sxed);
}


static
gboolean
sxed_delete_event (GtkWidget *widget, GdkEvent *event, gpointer ud)
{
    GncSxEditorDialog2 *sxed = (GncSxEditorDialog2*)ud;

    /* We've already processed the SX, likely because of "ok" being
     * clicked. */
    if (sxed->sx == NULL)
    {
        return FALSE;
    }

    if (!sxed_confirmed_cancel (sxed))
    {
        return TRUE;
    }
    return FALSE;
}


/*************************************
 * Create the Schedule Editor Dialog *
 ************************************/
GncSxEditorDialog2 *
gnc_ui_scheduled_xaction_editor_dialog_create2 (GtkWindow *parent, 
    SchedXaction *sx, gboolean newSX)
{
    GncSxEditorDialog2 *sxed;
    GtkBuilder *builder;
    GtkWidget *button;
    int i;
    GList *dlgExists = NULL;

    static struct widgetSignalCallback
    {
        char     *name;
        char     *signal;
        void     (*fn)();
        gpointer objectData;
    } widgets[] =
    {
        { "ok_button",      "clicked",       editor_ok_button_clicked_cb,     NULL },
        { "cancel_button",  "clicked",       editor_cancel_button_clicked_cb, NULL },
        { "help_button",    "clicked",       editor_help_button_clicked_cb,   NULL },
        { "rb_noend",       "toggled",       endgroup_rb_toggled_cb,          GINT_TO_POINTER(END_NEVER_OPTION) },
        { "rb_enddate",     "toggled",       endgroup_rb_toggled_cb,          GINT_TO_POINTER(END_DATE_OPTION) },
        { "rb_num_occur",   "toggled",       endgroup_rb_toggled_cb,          GINT_TO_POINTER(NUM_OCCUR_OPTION) },
        { "remain_spin" ,   "value-changed", sxed_excal_update_adapt_cb,      NULL },
        { "enabled_opt",    "toggled",       enabled_toggled_cb,              NULL },
        { "autocreate_opt", "toggled",       autocreate_toggled_cb,           NULL },
        { "advance_opt",    "toggled",       advance_toggled_cb,              NULL },
        { "remind_opt",     "toggled",       remind_toggled_cb,               NULL },
        { NULL,             NULL,            NULL,                            NULL }
    };

    dlgExists = gnc_find_gui_components (DIALOG_SCHEDXACTION2_EDITOR_CM_CLASS,
                                         editor_component_sx_equality,
                                         sx);
    if (dlgExists != NULL)
    {
        DEBUG ("dialog already exists; using that one.");
        sxed = (GncSxEditorDialog2*)dlgExists->data;
        gtk_window_present (GTK_WINDOW (sxed->dialog));
        g_list_free (dlgExists);
        return sxed;
    }

    sxed = g_new0 (GncSxEditorDialog2, 1);

    sxed->sx     = sx;
    sxed->newsxP = newSX;

    /* Load up Glade file */
    builder = gtk_builder_new();
    gnc_builder_add_from_file (builder, "dialog-sx.glade", "advance_days_adj");
    gnc_builder_add_from_file (builder, "dialog-sx.glade", "remind_days_adj");
    gnc_builder_add_from_file (builder, "dialog-sx.glade", "end_spin_adj");
    gnc_builder_add_from_file (builder, "dialog-sx.glade", "remain_spin_adj");
    gnc_builder_add_from_file (builder, "dialog-sx.glade", "scheduled_transaction_editor_dialog");

    sxed->builder = builder;

    /* Connect the Widgets */
    sxed->dialog = GTK_WIDGET (gtk_builder_get_object (builder, "scheduled_transaction_editor_dialog"));
    sxed->notebook = GTK_NOTEBOOK (gtk_builder_get_object (builder, "editor_notebook"));
    sxed->nameEntry = GTK_EDITABLE (gtk_builder_get_object (builder, "sxe_name"));
    sxed->enabledOpt = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "enabled_opt"));
    sxed->autocreateOpt = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "autocreate_opt"));
    sxed->notifyOpt = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "notify_opt"));
    sxed->advanceOpt = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "advance_opt"));
    sxed->advanceSpin = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "advance_days"));
    sxed->remindOpt = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "remind_opt"));
    sxed->remindSpin = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "remind_days"));
    sxed->lastOccurLabel = GTK_LABEL (gtk_builder_get_object (builder, "last_occur_label"));
    sxed->optEndNone = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "rb_noend"));
    sxed->optEndDate = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "rb_enddate"));
    sxed->optEndCount = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "rb_num_occur"));
    sxed->endCountSpin = GTK_ENTRY (gtk_builder_get_object (builder, "end_spin"));
    sxed->endRemainSpin = GTK_ENTRY (gtk_builder_get_object (builder, "remain_spin"));

    // Set the name of this dialog so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(sxed->dialog), "gnc-id-sx2-editor");
    gnc_widget_style_context_add_class (GTK_WIDGET(sxed->dialog), "gnc-class-sx");

    gtk_window_set_transient_for (GTK_WINDOW (sxed->dialog), parent);

    /* Setup the end-date GNC widget */
    {
        GtkWidget *endDateBox = GTK_WIDGET(gtk_builder_get_object (builder, "editor_end_date_box"));
        sxed->endDateEntry = GNC_DATE_EDIT (gnc_date_edit_new (gnc_time (NULL), FALSE, FALSE));
        gtk_widget_show (GTK_WIDGET (sxed->endDateEntry));
        g_signal_connect (sxed->endDateEntry, "date-changed",
                          G_CALLBACK (sxed_excal_update_adapt_cb), sxed);
        gtk_box_pack_start (GTK_BOX (endDateBox), GTK_WIDGET (sxed->endDateEntry),
                            TRUE, TRUE, 0 );
    }

    gnc_register_gui_component (DIALOG_SCHEDXACTION2_EDITOR_CM_CLASS,
                                NULL, /* no refresh handler */
                                sxed_close_handler,
                                sxed);

    g_signal_connect (sxed->dialog, "delete_event",
                      G_CALLBACK (sxed_delete_event), sxed);
    g_signal_connect (sxed->dialog, "destroy",
                      G_CALLBACK (scheduledxaction_editor_dialog_destroy),
                      sxed );

    for (i = 0; widgets[i].name != NULL; i++)
    {
        button = GTK_WIDGET (gtk_builder_get_object (builder, widgets[i].name));
        if (widgets[i].objectData != NULL)
        {
            g_object_set_data (G_OBJECT (button), "whichOneAmI",
                               widgets[i].objectData);
        }
        g_signal_connect (button, widgets[i].signal,
                          G_CALLBACK (widgets[i].fn ), sxed);
    }

    /* Set sensitivity settings  */
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->notifyOpt), FALSE );
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->advanceSpin), FALSE );
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->remindSpin), FALSE );
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->endCountSpin), FALSE );
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->endRemainSpin), FALSE );
    gtk_editable_set_editable (GTK_EDITABLE (sxed->advanceSpin), TRUE );
    gtk_editable_set_editable (GTK_EDITABLE (sxed->remindSpin), TRUE );

    /* Allow resize */
    gtk_window_set_resizable (GTK_WINDOW (sxed->dialog), TRUE);
    gnc_restore_window_size (GNC_PREFS_GROUP_SXED, GTK_WINDOW (sxed->dialog), parent);

    /* create the frequency-selection widget and example [dense-]calendar. */
    schedXact_editor_create_freq_sel (sxed);

    /* create the template-transaction ledger window */
    schedXact_editor_create_ledger (sxed);

    /* populate */
    schedXact_editor_populate (sxed);

    /* Do not call show_all here. Screws up the gtkuimanager code */
    gtk_widget_show (sxed->dialog);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (sxed->notebook), 0);

    /* Refresh the cal and the ledger */
    gtk_widget_queue_resize (GTK_WIDGET (sxed->example_cal));

    gnc_ledger_display2_refresh (sxed->ledger);

    /* Move keyboard focus to the name entry */
    gtk_widget_grab_focus (GTK_WIDGET (sxed->nameEntry));

    gtk_builder_connect_signals_full (builder, gnc_builder_connect_full_func, sxed);
    g_object_unref (G_OBJECT (builder));

    return sxed;
}


static
void
schedXact_editor_create_freq_sel (GncSxEditorDialog2 *sxed)
{
    GtkBox *b;

    b = GTK_BOX (gtk_builder_get_object (sxed->builder, "gncfreq_hbox"));

    sxed->gncfreq =
        GNC_FREQUENCY (gnc_frequency_new_from_recurrence (gnc_sx_get_schedule (sxed->sx),
                      xaccSchedXactionGetStartDate (sxed->sx)));
    g_assert (sxed->gncfreq);
    g_signal_connect (sxed->gncfreq, "changed",
                      G_CALLBACK (gnc_sxed_freq_changed),
                      sxed );
    gtk_container_add (GTK_CONTAINER (b), GTK_WIDGET (sxed->gncfreq) );

    b = GTK_BOX(gtk_builder_get_object (sxed->builder, "example_cal_hbox" ));
    sxed->dense_cal_model = gnc_dense_cal_store_new (EX_CAL_NUM_MONTHS * 31);
    sxed->example_cal = GNC_DENSE_CAL(gnc_dense_cal_new_with_model (GTK_WINDOW(sxed->dialog),
                                                                    GNC_DENSE_CAL_MODEL (sxed->dense_cal_model)));
    g_assert (sxed->example_cal);
    gnc_dense_cal_set_num_months (sxed->example_cal, EX_CAL_NUM_MONTHS);
    gnc_dense_cal_set_months_per_col( sxed->example_cal, EX_CAL_MO_PER_COL);
    gtk_container_add (GTK_CONTAINER (b), GTK_WIDGET (sxed->example_cal));
    gtk_widget_show (GTK_WIDGET (sxed->example_cal));
}


static
void
schedXact_editor_create_ledger (GncSxEditorDialog2 *sxed)
{
    GncTreeModelSplitReg *model;
    GtkWidget *main_vbox;
    GtkWidget *label;

    /* Create the ledger */
    sxed->sxGUIDstr = guid_to_string (xaccSchedXactionGetGUID (sxed->sx));
    sxed->ledger = gnc_ledger_display2_template_gl (sxed->sxGUIDstr);
    model = gnc_ledger_display2_get_split_model_register (sxed->ledger);

    /* First the embedded window */
    main_vbox = GTK_WIDGET (gtk_builder_get_object (sxed->builder, "register_vbox" ));
    sxed->embed_window =
        gnc_embedded_window_new ("SXWindowActions",
                                gnc_sxed_menu_entries,
                                gnc_sxed_menu_n_entries,
                                "gnc-sxed-window-ui.xml",
                                sxed->dialog,
                                FALSE, /* no accelerators */
                                sxed);

    gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (sxed->embed_window), TRUE, TRUE, 0);

    label = gtk_label_new (_("Note: If you have already accepted changes to the Template, Cancel will not revoke them."));
    gtk_box_pack_end (GTK_BOX (main_vbox), GTK_WIDGET (label), FALSE, TRUE, 0);
    gtk_widget_show (label);

    /* Now create the register plugin page. */
    sxed->plugin_page = gnc_plugin_page_register2_new_ledger (sxed->ledger);
    gnc_plugin_page_set_ui_description (sxed->plugin_page,
                                        "gnc-sxed-window-ui-full.xml");
    gnc_plugin_page_register2_set_options (sxed->plugin_page,
                                          NUM_LEDGER_LINES_DEFAULT, FALSE );

    gnc_embedded_window_open_page (sxed->embed_window, sxed->plugin_page);

    /* configure... */
    /* use double-line, so scheduled transaction Notes can be edited */
    gnc_tree_model_split_reg_config (model, model->type, model->style, TRUE);
}



static
void
schedXact_editor_populate (GncSxEditorDialog2 *sxed)
{
    char *name;
    time64 tmpDate;
    GncTreeModelSplitReg *model;
    const GDate *gd;
    gint daysInAdvance;
    gboolean enabledState, autoCreateState, notifyState;

    name = xaccSchedXactionGetName (sxed->sx);
    if (name != NULL)
    {
        gtk_entry_set_text (GTK_ENTRY (sxed->nameEntry), name);
    }
    {
        gd = xaccSchedXactionGetLastOccurDate (sxed->sx);
        if (g_date_valid (gd))
        {
            gchar dateBuf[ MAX_DATE_LENGTH+1 ];
            qof_print_gdate (dateBuf, MAX_DATE_LENGTH, gd);
            gtk_label_set_text (sxed->lastOccurLabel, dateBuf);
        }
        else
        {
            gtk_label_set_text (sxed->lastOccurLabel, _("(never)"));
        }
        gd = NULL;
    }

    gd = xaccSchedXactionGetEndDate (sxed->sx);
    if ( g_date_valid (gd))
    {
        gtk_toggle_button_set_active (sxed->optEndDate, TRUE);
        tmpDate = gnc_time64_get_day_start_gdate (gd);
        gnc_date_edit_set_time (sxed->endDateEntry, tmpDate);

        set_endgroup_toggle_states (sxed, END_DATE);
    }
    else if (xaccSchedXactionHasOccurDef (sxed->sx))
    {
        gint numOccur = xaccSchedXactionGetNumOccur (sxed->sx);
        gint numRemain = xaccSchedXactionGetRemOccur (sxed->sx);

        gtk_toggle_button_set_active (sxed->optEndCount, TRUE);

        gtk_spin_button_set_value (GTK_SPIN_BUTTON (sxed->endCountSpin), numOccur);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (sxed->endRemainSpin), numRemain);

        set_endgroup_toggle_states (sxed, END_OCCUR);
    }
    else
    {
        gtk_toggle_button_set_active (sxed->optEndNone, TRUE);
        set_endgroup_toggle_states (sxed, END_NEVER);
    }

    enabledState = xaccSchedXactionGetEnabled (sxed->sx);
    gtk_toggle_button_set_active (sxed->enabledOpt, enabledState);

    /* Do auto-create/notify setup */
    if (sxed->newsxP)
    {
        autoCreateState =
            gnc_prefs_get_bool (GNC_PREFS_GROUP_SXED, GNC_PREF_CREATE_AUTO);
        notifyState =
            gnc_prefs_get_bool (GNC_PREFS_GROUP_SXED, GNC_PREF_NOTIFY);
    }
    else
    {
        xaccSchedXactionGetAutoCreate (sxed->sx,
                                       &autoCreateState,
                                       &notifyState);
    }
    gtk_toggle_button_set_active (sxed->autocreateOpt, autoCreateState);
    if (!autoCreateState)
    {
        notifyState = FALSE;
    }
    gtk_toggle_button_set_active( sxed->notifyOpt, notifyState );

    /* Do days-in-advance-to-create widget[s] setup. */
    if ( sxed->newsxP )
    {
        daysInAdvance =
            gnc_prefs_get_float (GNC_PREFS_GROUP_SXED, GNC_PREF_CREATE_DAYS);
    }
    else
    {
        daysInAdvance =
            xaccSchedXactionGetAdvanceCreation (sxed->sx);
    }
    if (daysInAdvance != 0)
    {
        gtk_toggle_button_set_active (sxed->advanceOpt, TRUE);
        gtk_spin_button_set_value (sxed->advanceSpin,
                                   (gfloat)daysInAdvance);
    }

    /* Do days-in-advance-to-remind widget[s] setup. */
    if (sxed->newsxP)
    {
        daysInAdvance =
            gnc_prefs_get_float (GNC_PREFS_GROUP_SXED, GNC_PREF_REMIND_DAYS);
    }
    else
    {
        daysInAdvance =
            xaccSchedXactionGetAdvanceReminder (sxed->sx);
    }
    if (daysInAdvance != 0)
    {
        gtk_toggle_button_set_active (sxed->remindOpt, TRUE);
        gtk_spin_button_set_value (sxed->remindSpin,
                                   (gfloat)daysInAdvance);
    }

    if (sxed->newsxP)
    {
        gnc_sx_set_instance_count (sxed->sx, 1);
    }

    /* populate the ledger */
    {
        /* create the split list */
        GList        *splitList;

        splitList = xaccSchedXactionGetSplits (sxed->sx);
        if (splitList != NULL)
        {
            model = gnc_ledger_display2_get_split_model_register (sxed->ledger);
            gnc_tree_model_split_reg_load (model, splitList, NULL );
        } /* otherwise, use the existing stuff. */
    }

    /* Update the example cal */
    gnc_sxed_update_cal (sxed);
}


static
void
set_endgroup_toggle_states (GncSxEditorDialog2 *sxed, EndType type)
{
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->endDateEntry), (type == END_DATE) );
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->endCountSpin), (type == END_OCCUR) );
    gtk_widget_set_sensitive (GTK_WIDGET (sxed->endRemainSpin), (type == END_OCCUR) );
}


static
void
endgroup_rb_toggled_cb (GtkButton *b, gpointer d)
{
    /* figure out which one */
    GncSxEditorDialog2 *sxed;
    gint id;

    sxed = (GncSxEditorDialog2*)d;
    id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "whichOneAmI"));

    switch (id)
    {
    case END_NEVER_OPTION:
        set_endgroup_toggle_states (sxed, END_NEVER);
        break;
    case END_DATE_OPTION:
        set_endgroup_toggle_states (sxed, END_DATE);
        break;
    case NUM_OCCUR_OPTION:
        set_endgroup_toggle_states (sxed, END_OCCUR);
        break;
    default:
        g_critical ("Unknown id %d", id);
        break;
    }
    gnc_sxed_update_cal (sxed);
}


/********************************************************************\
 * gnc_register_check_close                                         *
 *                                                                  *
 * Args:   regData - the data struct for this register              *
 * Return: none                                                     *
\********************************************************************/
static void
gnc_sxed_reg_check_close (GncSxEditorDialog2 *sxed)
{
    GncTreeViewSplitReg *view;
    Transaction *dirty_trans;

    const char *message =
        _("The current template transaction "
          "has been changed. "
          "Would you like to record the changes?");

    view = gnc_ledger_display2_get_split_view_register (sxed->ledger);

    dirty_trans = gnc_tree_view_split_reg_get_dirty_trans (view);

    if (dirty_trans == NULL)
        return;

    if (gnc_verify_dialog (GTK_WINDOW (sxed->dialog), TRUE, "%s", message))
    {
        /* Save the template transactions changes */
        xaccTransCommitEdit (dirty_trans);
        gnc_tree_view_split_reg_set_dirty_trans (view, NULL);
        return;
    }
    else
    {
        /* Cancel the template transactions changes */
        gnc_tree_view_split_reg_cancel_edit (view, TRUE);
    }
}


static gboolean
editor_component_sx_equality (gpointer find_data,
                              gpointer user_data)
{
    return ((SchedXaction*)find_data
             == ((GncSxEditorDialog2*)user_data)->sx);
}
/*
typedef enum { NO_END, DATE_END, COUNT_END } END_TYPE;
*/

static void
gnc_sxed_update_cal (GncSxEditorDialog2 *sxed)
{
    GList *recurrences = NULL;
    GDate start_date, first_date;

    g_date_clear (&start_date, 1);

    gnc_frequency_save_to_recurrence (sxed->gncfreq, &recurrences, &start_date);
    g_date_subtract_days (&start_date, 1);
    recurrenceListNextInstance (recurrences, &start_date, &first_date);

    /* Deal with the fact that this SX may have been run before [the
     * calendar should only show upcoming instances]... */
    {
        const GDate *last_sx_inst;

        last_sx_inst = xaccSchedXactionGetLastOccurDate (sxed->sx);
        if (g_date_valid (last_sx_inst)
                && g_date_valid (&first_date)
                && g_date_compare (last_sx_inst, &first_date) != 0)
        {
            /* last occurrence will be passed as initial date to update store
             * later on as well */
            start_date = *last_sx_inst;
            recurrenceListNextInstance (recurrences, &start_date, &first_date);
        }
        else
            /* move one day back so the store can get the proper first recurrence. */
            g_date_subtract_days(&start_date, 1);
    }

    if (!g_date_valid (&first_date))
    {
        /* Nothing to do. */
        gnc_dense_cal_store_clear (sxed->dense_cal_model);
        goto cleanup;
    }

    gnc_dense_cal_store_update_name (sxed->dense_cal_model, xaccSchedXactionGetName (sxed->sx));
    {
        gchar *schedule_desc = recurrenceListToCompactString (recurrences);
        gnc_dense_cal_store_update_info (sxed->dense_cal_model, schedule_desc);
        g_free (schedule_desc);
    }

    //gnc_dense_cal_set_month(sxed->example_cal, g_date_get_month(&first_date));
    //gnc_dense_cal_set_year(sxed->example_cal, g_date_get_year(&first_date));

    /* figure out the end restriction */
    if (gtk_toggle_button_get_active (sxed->optEndDate))
    {
        GDate end_date;
        g_date_clear (&end_date, 1);
        gnc_gdate_set_time64 (&end_date, gnc_date_edit_get_date (sxed->endDateEntry));
        gnc_dense_cal_store_update_recurrences_date_end (sxed->dense_cal_model, &start_date, recurrences, &end_date);
    }
    else if (gtk_toggle_button_get_active (sxed->optEndNone))
    {
        gnc_dense_cal_store_update_recurrences_no_end (sxed->dense_cal_model, &start_date, recurrences);
    }
    else if (gtk_toggle_button_get_active (sxed->optEndCount))
    {
        gint num_remain
        = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sxed->endRemainSpin));
        gnc_dense_cal_store_update_recurrences_count_end (sxed->dense_cal_model, &start_date, recurrences, num_remain);
    }
    else
    {
        g_error ("unknown end condition");
    }

cleanup:
    recurrenceListFree (&recurrences);
}


static void
gnc_sxed_freq_changed (GncFrequency *gf, gpointer ud)
{
    gnc_sxed_update_cal ((GncSxEditorDialog2*)ud);
}


static void
sxed_excal_update_adapt_cb (GtkWidget *o, gpointer ud)
{
    gnc_sxed_update_cal ((GncSxEditorDialog2*)ud);
}


static void
on_sx_check_toggled_cb (GtkWidget *togglebutton, gpointer user_data)
{
    GtkWidget *widget_notify;
    GHashTable *table;

    PINFO("Togglebutton is %p and user_data is %p", togglebutton, user_data);
    PINFO("Togglebutton builder name is %s", gtk_buildable_get_name (GTK_BUILDABLE (togglebutton)));

    /* We need to use the hash table to find the required widget to activate. */
    table = g_object_get_data(G_OBJECT(user_data), "prefs_widget_hash");
    widget_notify = g_hash_table_lookup(table, "pref/" GNC_PREFS_GROUP_SXED "/" GNC_PREF_NOTIFY);

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(togglebutton)))
        gtk_widget_set_sensitive (widget_notify, TRUE);
    else
        gtk_widget_set_sensitive (widget_notify, FALSE);
}


/* ------------------------------------------------------------ */
/* sx app engine;  move to somewhere appropriate. :/            */

typedef struct _acct_deletion_handler_data
{
    GList *affected_sxes;
    GtkWidget *dialog;
    GtkWindow *parent;
} acct_deletion_handler_data;


static void
_open_editors (GtkDialog *dialog, gint response_code, gpointer data)
{
    acct_deletion_handler_data *adhd = (acct_deletion_handler_data *)data;
    gtk_widget_hide (adhd->dialog);
    {
        GList *sx_iter;
        for (sx_iter = adhd->affected_sxes; sx_iter; sx_iter = sx_iter->next)
        {
            gnc_ui_scheduled_xaction_editor_dialog_create2 (GTK_WINDOW(adhd->parent),
                (SchedXaction*)sx_iter->data, FALSE);
        }
    }
    g_list_free (adhd->affected_sxes);
    gtk_widget_destroy (GTK_WIDGET (adhd->dialog));
    g_free (adhd);
}


static void
_sx_engine_event_handler (QofInstance *ent, QofEventId event_type, gpointer user_data, gpointer evt_data)
{
    Account *acct;
    QofBook *book;
    GList *affected_sxes;

    if (!(event_type & QOF_EVENT_DESTROY))
        return;
    if (!GNC_IS_ACCOUNT(ent))
        return;
    acct = GNC_ACCOUNT(ent);
    book = qof_instance_get_book (QOF_INSTANCE (acct));
    affected_sxes = gnc_sx_get_sxes_referencing_account (book, acct);

    if (!gnc_list_length_cmp (affected_sxes, 0))
        return;

    {
        GList *sx_iter;
        acct_deletion_handler_data *data;
        GtkBuilder *builder;
        GtkWidget *dialog;
        GtkWindow *parent;
        GtkListStore *name_list;
        GtkTreeView *list;
        GtkTreeViewColumn *name_column;
        GtkCellRenderer *renderer;

        builder = gtk_builder_new();
        gnc_builder_add_from_file (builder, "dialog-sx.glade", "account_deletion_dialog");

        dialog = GTK_WIDGET (gtk_builder_get_object (builder, "account_deletion_dialog"));
        parent = gnc_ui_get_main_window (NULL);
        
        gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

        list = GTK_TREE_VIEW (gtk_builder_get_object (builder, "sx_list"));

        // Set grid lines option to preference
        gtk_tree_view_set_grid_lines (GTK_TREE_VIEW(list), gnc_tree_view_get_grid_lines_pref ());

        data = (acct_deletion_handler_data*)g_new0 (acct_deletion_handler_data, 1);
        data->dialog = dialog;
        data->parent = parent;
        data->affected_sxes = affected_sxes;
        name_list = gtk_list_store_new (1, G_TYPE_STRING);
        for (sx_iter = affected_sxes; sx_iter != NULL; sx_iter = sx_iter->next)
        {
            SchedXaction *sx;
            GtkTreeIter iter;
            gchar *sx_name;

            sx = (SchedXaction*)sx_iter->data;
            sx_name = xaccSchedXactionGetName (sx);
            gtk_list_store_append (name_list, &iter);
            gtk_list_store_set (name_list, &iter, 0, sx_name, -1);
        }
        gtk_tree_view_set_model (list, GTK_TREE_MODEL (name_list));
        g_object_unref (G_OBJECT (name_list));

        renderer = gtk_cell_renderer_text_new();
        name_column = gtk_tree_view_column_new_with_attributes (_("Name"),
                      renderer,
                      "text", 0, NULL);
        gtk_tree_view_append_column (list, name_column);

        g_signal_connect (G_OBJECT (dialog), "response",
                         G_CALLBACK (_open_editors), data);

        gtk_widget_show_all (GTK_WIDGET (dialog));
        gtk_builder_connect_signals_full (builder, gnc_builder_connect_full_func, data);
        g_object_unref (G_OBJECT (builder));
    }
}


void
gnc_ui_sx_initialize2 (void) //FIXME need to remove the 2 when live
{
    _sx_engine_event_handler_id = qof_event_register_handler (_sx_engine_event_handler, NULL);

    gnc_hook_add_dangler (HOOK_BOOK_OPENED,
                          (GFunc)gnc_sx_sxsincelast_book_opened, NULL, NULL);

    /* Add page to preferences page for Scheduled Transactions */
    /* The parameters are; glade file, items to add from glade file - last being the dialog, preference tab name */
    gnc_preferences_add_page ("dialog-sx.glade",
                              "create_days_adj,remind_days_adj,sx_prefs",
                              _("Scheduled Transactions"));
}
