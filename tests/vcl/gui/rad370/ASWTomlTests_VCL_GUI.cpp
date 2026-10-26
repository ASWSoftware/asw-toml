/* **************************************************************************
ASWTomlTests_VCL_GUI.cpp
Author: Anthony S. West - ASW Software

VCL GUI app that runs the ASWToml unit tests with the ASWUnitTests
framework's GUI runner (third_party\asw-unit-tests\vcl\gui\src).

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
#include <vcl.h>
#pragma hdrstop
#include <tchar.h>
//---------------------------------------------------------------------------
#include <exception>
#include <string>
//---------------------------------------------------------------------------
#include "ASWUnitTests_GUI_CommandLine.h"
#include "ASWUnitTests_GUI_TextDialog.h"
//---------------------------------------------------------------------------
USEFORM("..\..\..\..\third_party\asw-unit-tests\vcl\gui\src\ASWUnitTests_GUI_MainForm.cpp", FormASWUnitTestsGUIMain);
//---------------------------------------------------------------------------
#include "ASWUnitTests_GUI_MainForm.h"
//---------------------------------------------------------------------------
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR, int)
{
    int exitCode = ASWUnitTests::ExitCode_Success;

    try
    {
        Application->Initialize();
        Application->MainFormOnTaskBar = true;
        Application->Title = "ASWTomlTests";

        // Parsed before the main form exists, so --help, --version, and argument errors are shown on their own
        // and the process exits with the same code the console runner would use, without opening the GUI.
        ASWUnitTests::TGUIOptions options;
        ASWUnitTests::TGUICommandLineResult const commandLine =
            ASWUnitTests::TGUICommandLine::Parse(ASWUnitTests::TGUICommandLine::ProcessArguments(), options);

        if (commandLine.EarlyExitCode.has_value())
        {
            ASWUnitTests::ShowTextDialog(Application->Title, commandLine.Message);
            return *commandLine.EarlyExitCode;
        }

        Application->CreateForm(__classid(TFormASWUnitTestsGUIMain), &FormASWUnitTestsGUIMain);
        FormASWUnitTestsGUIMain->Start(options);
        Application->Run();

        // The form outlives Run(); Application frees it later, during shutdown.
        exitCode = FormASWUnitTestsGUIMain->ExitCode();

        // After a timeout, the abandoned test's thread may still be running, using one of the form's test groups,
        // and after a crash their state may be corrupted, so the process ends here, as the console runner's does,
        // without destroying them. The layout and selection were saved as the window closed.
        if (FormASWUnitTestsGUIMain->WasRunAborted())
            ExitProcess(static_cast<UINT>(exitCode));
    }
    catch (Exception& exception)
    {
        Application->ShowException(&exception);
        exitCode = ASWUnitTests::ExitCode_UnhandledException;
    }
    catch (std::exception const& exception)
    {
        ASWUnitTests::ShowTextDialog(Application->Title, std::string("Unhandled exception: ") + exception.what());
        exitCode = ASWUnitTests::ExitCode_UnhandledException;
    }
    catch (...)
    {
        try
        {
            throw Exception("");
        }
        catch (Exception& exception)
        {
            Application->ShowException(&exception);
        }

        exitCode = ASWUnitTests::ExitCode_UnhandledExceptionUnknown;
    }

    return exitCode;
}
//---------------------------------------------------------------------------
