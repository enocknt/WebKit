/*
 * Copyright (C) 2012 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "WebViewTest.h"
#include <WebCore/GtkVersioning.h>
#include <glib/gstdio.h>
#include <wtf/glib/GRefPtr.h>

#ifdef HAVE_GTK_UNIX_PRINTING
#include <gtk/gtkunixprint.h>
#endif

static void testPrintOperationPrintSettings(WebViewTest* test, gconstpointer)
{
    GRefPtr<WebKitPrintOperation> printOperation = adoptGRef(webkit_print_operation_new(test->webView()));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(printOperation.get()));

    g_assert_null(webkit_print_operation_get_print_settings(printOperation.get()));
    g_assert_null(webkit_print_operation_get_page_setup(printOperation.get()));

    GRefPtr<GtkPrintSettings> printSettings = adoptGRef(gtk_print_settings_new());
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(printSettings.get()));

    GRefPtr<GtkPageSetup> pageSetup = adoptGRef(gtk_page_setup_new());
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(pageSetup.get()));

    webkit_print_operation_set_print_settings(printOperation.get(), printSettings.get());
    webkit_print_operation_set_page_setup(printOperation.get(), pageSetup.get());

    g_assert_true(webkit_print_operation_get_print_settings(printOperation.get()) == printSettings.get());
    g_assert_true(webkit_print_operation_get_page_setup(printOperation.get()) == pageSetup.get());
}

static gboolean webViewPrintCallback(WebKitWebView* webView, WebKitPrintOperation* printOperation, WebViewTest* test)
{
    g_assert_true(webView == test->webView());

    g_assert_true(WEBKIT_IS_PRINT_OPERATION(printOperation));
    test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(printOperation));

    g_assert_null(webkit_print_operation_get_print_settings(printOperation));
    g_assert_null(webkit_print_operation_get_page_setup(printOperation));

    g_main_loop_quit(test->m_mainLoop);

    return TRUE;
}

static void testWebViewPrint(WebViewTest* test, gconstpointer)
{
    g_signal_connect(test->webView(), "print", G_CALLBACK(webViewPrintCallback), test);
    test->loadHtml("<html><body onLoad=\"print();\">WebKitGTK printing test</body></html>", 0);
    g_main_loop_run(test->m_mainLoop);
}

#ifdef HAVE_GTK_UNIX_PRINTING
static GtkPrinter* findPrintToFilePrinter()
{
    GtkPrinter* printer = nullptr;
    gtk_enumerate_printers([](GtkPrinter* printer, gpointer userData) -> gboolean {
        auto* backend = gtk_printer_get_backend(printer);
        if (!strcmp(G_OBJECT_TYPE_NAME(backend), "GtkPrintBackendFile")) {
            auto** foundPrinter = static_cast<GtkPrinter**>(userData);
            *foundPrinter = static_cast<GtkPrinter*>(g_object_ref(printer));
            return TRUE;
        }
        return FALSE;
    }, &printer, nullptr, TRUE);
    return printer;
}

class PrintTest: public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(PrintTest);

    static void printFinishedCallback(WebKitPrintOperation*, PrintTest* test)
    {
        g_assert_cmpuint(test->m_expectedError, ==, 0);
        g_main_loop_quit(test->m_mainLoop);
    }

    static void printFailedCallback(WebKitPrintOperation*, GError* error, PrintTest* test)
    {
        g_assert_cmpuint(test->m_expectedError, !=, 0);
        g_assert_nonnull(error);
        g_assert_error(error, WEBKIT_PRINT_ERROR, test->m_expectedError);
        test->m_expectedError = 0;
    }

    PrintTest()
    {
        m_printOperation = adoptGRef(webkit_print_operation_new(m_webView.get()));
        assertObjectIsDeletedWhenTestFinishes(G_OBJECT(m_printOperation.get()));
        g_signal_connect(m_printOperation.get(), "finished", G_CALLBACK(printFinishedCallback), this);
        g_signal_connect(m_printOperation.get(), "failed", G_CALLBACK(printFailedCallback), this);
    }

    void waitUntilPrintFinished()
    {
        g_main_loop_run(m_mainLoop);
    }

    GRefPtr<WebKitPrintOperation> m_printOperation;
    int m_expectedError { 0 };
};

static void testPrintOperationPrint(PrintTest* test, gconstpointer)
{
    GRefPtr<GtkPrinter> printer = adoptGRef(findPrintToFilePrinter());
    if (!printer) {
        g_test_skip("no suitable printer found");
        return;
    }

    test->loadHtml("<html><body>WebKitGTK printing test</body></html>", 0);
    test->waitUntilLoadFinished();

    GUniquePtr<char> outputFilename(g_build_filename(Test::dataDirectory(), "webkit-print.pdf", nullptr));
    GRefPtr<GFile> outputFile = adoptGRef(g_file_new_for_path(outputFilename.get()));
    GUniquePtr<char> outputURI(g_file_get_uri(outputFile.get()));

    GRefPtr<GtkPrintSettings> printSettings = adoptGRef(gtk_print_settings_new());
    gtk_print_settings_set_printer(printSettings.get(), gtk_printer_get_name(printer.get()));
    gtk_print_settings_set(printSettings.get(), GTK_PRINT_SETTINGS_OUTPUT_URI, outputURI.get());

    webkit_print_operation_set_print_settings(test->m_printOperation.get(), printSettings.get());
    ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    webkit_print_operation_print(test->m_printOperation.get());
    ALLOW_DEPRECATED_DECLARATIONS_END
    test->waitUntilPrintFinished();

    GRefPtr<GFileInfo> fileInfo = adoptGRef(g_file_query_info(outputFile.get(),
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
        static_cast<GFileQueryInfoFlags>(0), 0, 0));
    g_assert_nonnull(fileInfo.get());
    g_assert_cmpint(g_file_info_get_size(fileInfo.get()), >, 0);
    g_assert_cmpstr(g_file_info_get_content_type(fileInfo.get()), ==, "application/pdf");

    g_file_delete(outputFile.get(), 0, 0);
}

static void testPrintOperationErrors(PrintTest* test, gconstpointer)
{
    GRefPtr<GtkPrinter> printer = adoptGRef(findPrintToFilePrinter());
    if (!printer) {
        g_test_skip("no suitable printer found");
        return;
    }

    test->loadHtml("<html><body>WebKitGTK printing errors test</body></html>", 0);
    test->waitUntilLoadFinished();

    // General Error: invalid filename.
    test->m_expectedError = WEBKIT_PRINT_ERROR_GENERAL;
    GRefPtr<GtkPrintSettings> printSettings = adoptGRef(gtk_print_settings_new());
    gtk_print_settings_set_printer(printSettings.get(), gtk_printer_get_name(printer.get()));
    gtk_print_settings_set(printSettings.get(), GTK_PRINT_SETTINGS_OUTPUT_URI, "file:///foo/bar");
    webkit_print_operation_set_print_settings(test->m_printOperation.get(), printSettings.get());
    ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    webkit_print_operation_print(test->m_printOperation.get());
    ALLOW_DEPRECATED_DECLARATIONS_END
    if (test->m_expectedError == WEBKIT_PRINT_ERROR_GENERAL)
        test->waitUntilPrintFinished();

    // Printer not found error.
    test->m_expectedError = WEBKIT_PRINT_ERROR_PRINTER_NOT_FOUND;
    gtk_print_settings_set_printer(printSettings.get(), "The fake WebKit printer");
    ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    webkit_print_operation_print(test->m_printOperation.get());
    ALLOW_DEPRECATED_DECLARATIONS_END
    if (test->m_expectedError == WEBKIT_PRINT_ERROR_PRINTER_NOT_FOUND)
        test->waitUntilPrintFinished();

    // No pages to print: print even pages for a single page document.
    test->m_expectedError = WEBKIT_PRINT_ERROR_INVALID_PAGE_RANGE;
    gtk_print_settings_set_printer(printSettings.get(), gtk_printer_get_name(printer.get()));
    gtk_print_settings_set_page_set(printSettings.get(), GTK_PAGE_SET_EVEN);
    ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    webkit_print_operation_print(test->m_printOperation.get());
    ALLOW_DEPRECATED_DECLARATIONS_END
    if (test->m_expectedError == WEBKIT_PRINT_ERROR_INVALID_PAGE_RANGE)
        test->waitUntilPrintFinished();
}

class CloseAfterPrintTest: public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(CloseAfterPrintTest);

    static GtkWidget* webViewCreate(WebKitWebView* webView, WebKitNavigationAction*, CloseAfterPrintTest* test)
    {
        return test->createWebView();
    }

    static gboolean webViewPrint(WebKitWebView* webView, WebKitPrintOperation* printOperation, CloseAfterPrintTest* test)
    {
        test->print(printOperation);
        return TRUE;
    }

    static void printOperationFinished(WebKitPrintOperation* printOperation, CloseAfterPrintTest* test)
    {
        test->printFinished();
    }

    static void webViewClosed(WebKitWebView* webView, CloseAfterPrintTest* test)
    {
        g_signal_handlers_disconnect_by_data(webView, test);
        g_object_unref(webView);
        test->m_webViewClosed = true;
        if (test->m_printFinished)
            g_main_loop_quit(test->m_mainLoop);
    }

    CloseAfterPrintTest()
    {
        webkit_settings_set_javascript_can_open_windows_automatically(webkit_web_view_get_settings(m_webView.get()), TRUE);
        g_signal_connect(m_webView.get(), "create", G_CALLBACK(webViewCreate), this);
    }

    GtkWidget* createWebView()
    {
        auto newWebView = Test::createWebView("related-view", m_webView.get(), nullptr);

        assertObjectIsDeletedWhenTestFinishes(G_OBJECT(newWebView.get()));
        g_signal_connect(newWebView.get(), "print", G_CALLBACK(webViewPrint), this);
        g_signal_connect(newWebView.get(), "close", G_CALLBACK(webViewClosed), this);
        return GTK_WIDGET(newWebView.leakRef());
    }

    void print(WebKitPrintOperation* printOperation)
    {
        assertObjectIsDeletedWhenTestFinishes(G_OBJECT(printOperation));

        GUniquePtr<char> outputFilename(g_build_filename(Test::dataDirectory(), "webkit-close-after-print.pdf", nullptr));
        m_outputFile = adoptGRef(g_file_new_for_path(outputFilename.get()));
        GUniquePtr<char> outputURI(g_file_get_uri(m_outputFile.get()));

        GRefPtr<GtkPrintSettings> printSettings = adoptGRef(gtk_print_settings_new());
        gtk_print_settings_set_printer(printSettings.get(), gtk_printer_get_name(m_printer.get()));
        gtk_print_settings_set(printSettings.get(), GTK_PRINT_SETTINGS_OUTPUT_URI, outputURI.get());
        webkit_print_operation_set_print_settings(printOperation, printSettings.get());

        m_printOperation = printOperation;
        g_signal_connect(m_printOperation.get(), "finished", G_CALLBACK(printOperationFinished), this);
        ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        webkit_print_operation_print(m_printOperation.get());
        ALLOW_DEPRECATED_DECLARATIONS_END
    }

    void printFinished()
    {
        m_printFinished = true;
        m_printOperation = nullptr;
        g_assert_nonnull(m_outputFile);
        g_file_delete(m_outputFile.get(), 0, 0);
        m_outputFile = nullptr;
        if (m_webViewClosed)
            g_main_loop_quit(m_mainLoop);
    }

    void waitUntilPrintFinishedAndViewClosed()
    {
        g_main_loop_run(m_mainLoop);
    }

    GRefPtr<GtkPrinter> m_printer;
    GRefPtr<WebKitPrintOperation> m_printOperation;
    GRefPtr<GFile> m_outputFile;
    bool m_webViewClosed { false };
    bool m_printFinished { false };
};

static void testPrintOperationCloseAfterPrint(CloseAfterPrintTest* test, gconstpointer)
{
    GRefPtr<GtkPrinter> printer = adoptGRef(findPrintToFilePrinter());
    if (!printer) {
        g_test_skip("no suitable printer found");
        return;
    }
    test->m_printer = WTFMove(printer);
    test->loadHtml("<html><body onLoad=\"w = window.open();w.print();w.close();\"></body></html>", 0);
    test->waitUntilPrintFinishedAndViewClosed();
}

#if !ENABLE(2022_GLIB_API)
class PrintCustomWidgetTest: public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(PrintCustomWidgetTest);

    static void applyCallback(WebKitPrintCustomWidget*, PrintCustomWidgetTest* test)
    {
        test->m_applyEmitted = true;
    }

    static gboolean scheduleJumpToCustomWidget(PrintCustomWidgetTest* test)
    {
        test->jumpToCustomWidget();

        return FALSE;
    }

    static void updateCallback(WebKitPrintCustomWidget* customWidget, GtkPageSetup*, GtkPrintSettings*, PrintCustomWidgetTest* test)
    {
        ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        g_assert_true(test->m_widget == webkit_print_custom_widget_get_widget(customWidget));
        ALLOW_DEPRECATED_DECLARATIONS_END

        test->m_updateEmitted = true;
        // Would be nice to avoid the 1 second timeout here - but I didn't found
        // a way to do so without making the test flaky.
        g_timeout_add_seconds(1, reinterpret_cast<GSourceFunc>(scheduleJumpToCustomWidget), test);
    }

    static void widgetRealizeCallback(GtkWidget* widget, PrintCustomWidgetTest* test)
    {
        g_assert_true(GTK_IS_LABEL(widget));
        g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(widget)), ==, "Label");

        test->m_widgetRealized = true;
        test->startPrinting();
    }

    static WebKitPrintCustomWidget* createCustomWidgetCallback(WebKitPrintOperation* printOperation, PrintCustomWidgetTest* test)
    {
        test->m_createEmitted = true;
        WebKitPrintCustomWidget* printCustomWidget = test->createPrintCustomWidget();
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(printCustomWidget));
        g_signal_connect(printCustomWidget, "apply", G_CALLBACK(applyCallback), test);
        g_signal_connect(printCustomWidget, "update", G_CALLBACK(updateCallback), test);

        ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        GtkWidget* widget = webkit_print_custom_widget_get_widget(printCustomWidget);
        ALLOW_DEPRECATED_DECLARATIONS_END
        test->assertObjectIsDeletedWhenTestFinishes(G_OBJECT(widget));
        g_signal_connect(widget, "realize", G_CALLBACK(widgetRealizeCallback), test);

        return printCustomWidget;
    }

    static gboolean scheduleMovementThroughDialog(PrintCustomWidgetTest* test)
    {
        test->jumpToFirstPrinter();

        return FALSE;
    }

    static gboolean openPrintDialog(PrintCustomWidgetTest* test)
    {
        g_idle_add(reinterpret_cast<GSourceFunc>(scheduleMovementThroughDialog), test);
        test->m_response = webkit_print_operation_run_dialog(test->m_printOperation.get(), GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(test->webView()))));

        return FALSE;
    }

    static void printOperationFinished(WebKitPrintOperation* printOperation, PrintCustomWidgetTest* test)
    {
        test->printFinished();
    }

    void printFinished()
    {
        g_assert_nonnull(m_outputFile);
        g_file_delete(m_outputFile.get(), nullptr, nullptr);
        m_outputFile = nullptr;
        g_main_loop_quit(m_mainLoop);
    }

    void createWebKitPrintOperation()
    {
        m_printOperation = adoptGRef(webkit_print_operation_new(m_webView.get()));
        g_assert_nonnull(m_printOperation);
        assertObjectIsDeletedWhenTestFinishes(G_OBJECT(m_printOperation.get()));

        g_signal_connect(m_printOperation.get(), "create-custom-widget", G_CALLBACK(createCustomWidgetCallback), this);
        g_signal_connect(m_printOperation.get(), "finished", G_CALLBACK(printOperationFinished), this);
    }

    WebKitPrintCustomWidget* createPrintCustomWidget()
    {
        m_widget = gtk_label_new("Label");
        ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        return webkit_print_custom_widget_new(m_widget, "Custom Widget");
        ALLOW_DEPRECATED_DECLARATIONS_END
    }

    void startPrinting()
    {
        // To start printing it is enough to press the Return key
        keyStroke(GDK_KEY_Return);
    }

    void jumpToFirstPrinter()
    {
        // Initially the GtkNotebook has focus, so we just need to press the Tab
        // key to jump to the first printer
        keyStroke(GDK_KEY_Tab);
    }

    void jumpToCustomWidget()
    {
        // Jump back to the GtkNotebook
        keyStroke(GDK_KEY_Tab, { WebViewTest::Modifiers::Shift });
        // Custom widget is on the third tab
        keyStroke(GDK_KEY_Right);
        keyStroke(GDK_KEY_Right);
    }

    void openDialogMoveThroughItAndWaitUntilClosed()
    {
        g_idle_add(reinterpret_cast<GSourceFunc>(openPrintDialog), this);
        g_main_loop_run(m_mainLoop);
    }

    GRefPtr<WebKitPrintOperation> m_printOperation;
    GRefPtr<GFile> m_outputFile;
    GtkWidget* m_widget;
    bool m_widgetRealized {false};
    bool m_applyEmitted {false};
    bool m_updateEmitted {false};
    bool m_createEmitted {false};
    WebKitPrintOperationResponse m_response {WEBKIT_PRINT_OPERATION_RESPONSE_CANCEL};
};

static void testPrintCustomWidget(PrintCustomWidgetTest* test, gconstpointer)
{
    GRefPtr<GtkPrinter> printer = adoptGRef(findPrintToFilePrinter());
    if (!printer) {
        g_test_skip("no suitable printer found");
        return;
    }

    test->showInWindow();
    test->loadHtml("<html><body>Text</body></html>", 0);
    test->waitUntilLoadFinished();

    test->createWebKitPrintOperation();

    GUniquePtr<char> outputFilename(g_build_filename(Test::dataDirectory(), "webkit-close-after-print.pdf", nullptr));
    test->m_outputFile = adoptGRef(g_file_new_for_path(outputFilename.get()));
    GUniquePtr<char> outputURI(g_file_get_uri(test->m_outputFile.get()));

    GRefPtr<GtkPrintSettings> printSettings = adoptGRef(gtk_print_settings_new());
    gtk_print_settings_set(printSettings.get(), GTK_PRINT_SETTINGS_OUTPUT_URI, outputURI.get());
    webkit_print_operation_set_print_settings(test->m_printOperation.get(), printSettings.get());

    test->openDialogMoveThroughItAndWaitUntilClosed();

    g_assert_cmpuint(test->m_response, ==, WEBKIT_PRINT_OPERATION_RESPONSE_PRINT);
    g_assert_true(test->m_createEmitted);
    g_assert_true(test->m_widgetRealized);
    g_assert_true(test->m_updateEmitted);
    g_assert_true(test->m_applyEmitted);
}
#endif // !ENABLE(2022_GLIB_API)
#endif // HAVE_GTK_UNIX_PRINTING

void beforeAll()
{
    WebViewTest::add("WebKitPrintOperation", "printing-settings", testPrintOperationPrintSettings);
    WebViewTest::add("WebKitWebView", "print", testWebViewPrint);
#ifdef HAVE_GTK_UNIX_PRINTING
    PrintTest::add("WebKitPrintOperation", "print", testPrintOperationPrint);
    PrintTest::add("WebKitPrintOperation", "print-errors", testPrintOperationErrors);
    CloseAfterPrintTest::add("WebKitPrintOperation", "close-after-print", testPrintOperationCloseAfterPrint);
#if !ENABLE(2022_GLIB_API)
    PrintCustomWidgetTest::add("WebKitPrintOperation", "custom-widget", testPrintCustomWidget);
#endif
#endif
}

void afterAll()
{
}
