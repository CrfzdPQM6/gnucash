/*
 * gnucash-cli.cpp -- The command line entry point for GnuCash
 *
 * Copyright (C) 2020 Geert Janssens <geert@kobaltwit.be>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
 * Boston, MA  02110-1301,  USA       gnu@gnu.org
 */
#include <config.h>

#include <libguile.h>
#include <guile-mappings.h>
#ifdef __MINGW32__
#include <Windows.h>
#include <fcntl.h>
#endif

#include "gnucash-commands.hpp"
#include "gnucash-core-app.hpp"

extern "C" {
#include <gnc-engine-guile.h>
#include <gnc-prefs.h>
#include <gnc-prefs-utils.h>
#include <gnc-gnome-utils.h>
#include <gnc-report.h>
#include <gnc-session.h>
#include <qoflog.h>
}

#include <boost/locale.hpp>
#include <fstream>
#include <iostream>

namespace bl = boost::locale;

static std::string empty_string{};

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_GUI;

static void
scm_cleanup_and_exit_with_failure (QofSession *session)
{
    if (session)
    {
        auto error{qof_session_get_error (session)};
        if (error != ERR_BACKEND_NO_ERR)
        {
            if (error == ERR_BACKEND_LOCKED)
                PERR ("File is locked, won't open.");
            else
                PERR ("Session Error: %s\n",
                      qof_session_get_error_message (session));
        }
        qof_session_destroy (session);
    }
    qof_event_resume();
    gnc_shutdown (1);
}

static void
scm_add_quotes(void *data, [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    auto add_quotes_file = static_cast<const std::string*>(data);

    scm_c_eval_string("(debug-set! stack 200000)");

    auto mod = scm_c_resolve_module("gnucash price-quotes");
    scm_set_current_module(mod);

    gnc_prefs_init ();
    qof_event_suspend();
    scm_c_eval_string("(gnc:price-quotes-install-sources)");

    if (!gnc_quote_source_fq_installed())
    {
        std::cerr << _("No quotes retrieved. Finance::Quote isn't "
                                    "installed properly.") << "\n";
        scm_cleanup_and_exit_with_failure (nullptr);
    }

    auto add_quotes = scm_c_eval_string("gnc:book-add-quotes");
    auto session = gnc_get_current_session();
    if (!session)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_begin(session, add_quotes_file->c_str(), SESSION_NORMAL_OPEN);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_load(session, NULL);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    auto scm_book = gnc_book_to_scm(qof_session_get_book(session));
    auto scm_result = scm_call_2(add_quotes, SCM_BOOL_F, scm_book);

    qof_session_save(session, NULL);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_destroy(session);
    if (!scm_is_true(scm_result))
    {
        PERR ("Failed to add quotes to %s.", add_quotes_file->c_str());
        scm_cleanup_and_exit_with_failure (session);
    }

    qof_event_resume();
    gnc_shutdown(0);
    return;
}

static void
report_session_percentage (const char *message, double percent)
{
    static double previous = 0.0;
    if ((percent - previous) < 5.0)
        return;
    PINFO ("\r%3.0f%% complete...", percent);
    previous = percent;
    return;
}

/* Don't try to use std::string& for the members of the following struct, it
 * results in the values getting corrupted as it passes through initializing
 * Scheme when compiled with Clang.
 */
struct run_report_args {
    const std::string& file_to_load;
    const std::string& run_report;
    const std::string& export_type;
    const std::string& output_file;
};

static inline void
write_report_file (const char *html, const char* file)
{
    if (!file || !html || !*html) return;
    std::ofstream ofs{file};
    if (!ofs)
    {
        std::cerr << "Failed to open file " << file << " for writing\n";
        return;
    }
    ofs << html << std::endl;
    // ofs destructor will close the file
}

static void
scm_run_report (void *data,
                [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    auto args = static_cast<run_report_args*>(data);

    scm_c_eval_string("(debug-set! stack 200000)");
    scm_c_use_module ("gnucash utilities");
    scm_c_use_module ("gnucash app-utils");
    scm_c_use_module ("gnucash reports");

    gnc_report_init ();
    Gnucash::gnc_load_scm_config();
    gnc_prefs_init ();
    qof_event_suspend ();

    auto datafile = args->file_to_load.c_str();
    auto check_report_cmd = scm_c_eval_string ("gnc:cmdline-check-report");
    auto get_report_cmd = scm_c_eval_string ("gnc:cmdline-get-report-id");
    auto run_export_cmd = scm_c_eval_string ("gnc:cmdline-template-export");
    /* We generally insist on using scm_from_utf8_string() throughout GnuCash
     * because all GUI-sourced strings and all file-sourced strings are encoded
     * that way. In this case, though, the input is coming from a shell window
     * and Microsoft Windows shells are generally not capable of entering UTF8
     * so it's necessary here to allow guile to read the locale and interpret
     * the input in that encoding.
     */
    auto report = scm_from_locale_string (args->run_report.c_str());
    auto type = !args->export_type.empty() ?
                scm_from_locale_string (args->export_type.c_str()) : SCM_BOOL_F;

    if (scm_is_false (scm_call_2 (check_report_cmd, report, type)))
        scm_cleanup_and_exit_with_failure (nullptr);

    PINFO ("Loading datafile %s...\n", datafile);

    auto session = gnc_get_current_session ();
    if (!session)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_begin (session, datafile, SESSION_READ_ONLY);
    if (qof_session_get_error (session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_load (session, report_session_percentage);
    if (qof_session_get_error (session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    char *output;

    if (!args->export_type.empty())
    {
        SCM retval = scm_call_2 (run_export_cmd, report, type);
        SCM query_result = scm_c_eval_string ("gnc:html-document?");
        SCM get_export_string = scm_c_eval_string ("gnc:html-document-export-string");
        SCM get_export_error = scm_c_eval_string ("gnc:html-document-export-error");

        if (scm_is_false (scm_call_1 (query_result, retval)))
        {
            std::cerr << _("This report must be upgraded to \
return a document object with export-string or export-error.") << std::endl;
            scm_cleanup_and_exit_with_failure (nullptr);
        }

        SCM export_string = scm_call_1 (get_export_string, retval);
        SCM export_error = scm_call_1 (get_export_error, retval);

        if (scm_is_string (export_string))
        {
            output = scm_to_utf8_string (export_string);
            if (!args->output_file.empty())
            {
                write_report_file(output, args->output_file.c_str());
            }
            else
            {
                std::cout << output << std::endl;
            }
        }
        else if (scm_is_string (export_error))
        {
            auto err = scm_to_utf8_string (export_error);
            std::cerr << err << std::endl;
            scm_cleanup_and_exit_with_failure (nullptr);
        }
        else
        {
            std::cerr << _("This report must be upgraded to \
return a document object with export-string or export-error.") << std::endl;
            scm_cleanup_and_exit_with_failure (nullptr);
        }
    }
    else
    {
        SCM id = scm_call_1(get_report_cmd, report);

        if (scm_is_false (id))
            scm_cleanup_and_exit_with_failure (nullptr);
        char *html, *errmsg;

        if (gnc_run_report_with_error_handling (scm_to_int(id), &html, &errmsg))
        {
            if (!args->output_file.empty())
            {
                write_report_file(html, args->output_file.c_str());
            }
            else
            {
                std::cout << html << std::endl;
            }
        }
        else
        {
            std::cerr << errmsg << std::endl;
        }
    }

    qof_session_destroy (session);

    qof_event_resume ();
    gnc_shutdown (0);
    return;
}


struct show_report_args {
    const std::string& file_to_load;
    const std::string& show_report;
};

static void
scm_report_show (void *data,
                [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    auto args = static_cast<show_report_args*>(data);

    scm_c_eval_string("(debug-set! stack 200000)");
    scm_c_use_module ("gnucash utilities");
    scm_c_use_module ("gnucash app-utils");
    scm_c_use_module ("gnucash reports");
    gnc_report_init ();
    Gnucash::gnc_load_scm_config();

    if (!args->file_to_load.empty())
    {
        auto datafile = args->file_to_load.c_str();
        PINFO ("Loading datafile %s...\n", datafile);

        auto session = gnc_get_current_session ();
        if (session)
        {
            qof_session_begin (session, datafile, SESSION_READ_ONLY);
            if (qof_session_get_error (session) == ERR_BACKEND_NO_ERR)
                qof_session_load (session, report_session_percentage);
        }
    }

    scm_call_2 (scm_c_eval_string ("gnc:cmdline-report-show"),
                scm_from_locale_string (args->show_report.c_str ()),
                scm_current_output_port ());
    gnc_shutdown (0);
    return;
}


static void
scm_report_list ([[maybe_unused]] void *data,
                 [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    scm_c_eval_string("(debug-set! stack 200000)");
    scm_c_use_module ("gnucash app-utils");
    scm_c_use_module ("gnucash reports");
    gnc_report_init ();
    Gnucash::gnc_load_scm_config();

    scm_call_1 (scm_c_eval_string ("gnc:cmdline-report-list"),
                scm_current_output_port ());
    gnc_shutdown (0);
    return;
}

int
Gnucash::add_quotes (const bo_str& uri)
{
    if (uri && !uri->empty())
        scm_boot_guile (0, nullptr, scm_add_quotes, (void *)&(*uri));

    return 0;
}

int
Gnucash::run_report (const bo_str& file_to_load,
                     const bo_str& run_report,
                     const bo_str& export_type,
                     const bo_str& output_file)
{
    auto args = run_report_args { file_to_load ? *file_to_load : empty_string,
                                  run_report ? *run_report : empty_string,
                                  export_type ? *export_type : empty_string,
                                  output_file ? *output_file : empty_string };
    if (run_report && !run_report->empty())
        scm_boot_guile (0, nullptr, scm_run_report, &args);

    return 0;
}

int
Gnucash::report_show (const bo_str& file_to_load,
                      const bo_str& show_report)
{
    auto args = show_report_args { file_to_load ? *file_to_load : empty_string,
                                   show_report ? *show_report : empty_string };
    if (show_report && !show_report->empty())
        scm_boot_guile (0, nullptr, scm_report_show, &args);

    return 0;
}

int
Gnucash::report_list (void)
{
    scm_boot_guile (0, nullptr, scm_report_list, NULL);
    return 0;
}
