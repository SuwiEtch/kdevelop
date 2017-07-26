/* KDevelop CMake Support
 *
 * Copyright 2012 Miha Čančula <miha@noughmad.eu>
 * Copyright 2017 Kevin Funk <kfunk@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "test_ctestfindsuites.h"

#include "testhelpers.h"
#include "cmake-test-paths.h"

#include <language/duchain/duchainlock.h>
#include <language/duchain/duchain.h>
#include <language/duchain/topducontext.h>
#include <interfaces/itestcontroller.h>
#include <interfaces/itestsuite.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iproject.h>
#include <testing/ctestsuite.h>
#include <tests/autotestshell.h>
#include <tests/testcore.h>
#include <project/projectmodel.h>

#include <QDir>
#include <QtTest>

using namespace KDevelop;

void waitForSuites(IProject* project, int count, int max)
{
    auto testController = ICore::self()->testController();
    for(int i = 0; testController->testSuitesForProject(project).size() < count && i < max * 10; ++i) {
        QSignalSpy spy(testController, &ITestController::testSuiteAdded);
        QVERIFY(spy.wait(1000));
    }
}

void TestCTestFindSuites::initTestCase()
{
    AutoTestShell::init({"kdevcmakemanager"});
    TestCore::initialize();

    cleanup();
}

void TestCTestFindSuites::cleanup()
{
    foreach(IProject* p, ICore::self()->projectController()->projects()) {
        ICore::self()->projectController()->closeProject(p);
    }
    QVERIFY(ICore::self()->projectController()->projects().isEmpty());
}

void TestCTestFindSuites::cleanupTestCase()
{
    TestCore::shutdown();
}

void TestCTestFindSuites::testCTestSuite()
{
    IProject* project = loadProject( "unit_tests" );
    QVERIFY2(project, "Project was not opened");
    waitForSuites(project, 5, 10);
    QList<ITestSuite*> suites = ICore::self()->testController()->testSuitesForProject(project);

    QCOMPARE(suites.size(), 5);

    DUChainReadLocker locker(DUChain::lock());

    foreach (auto suite, suites)
    {
        QCOMPARE(suite->cases(), QStringList());
        QVERIFY(!suite->declaration().isValid());
        CTestSuite* ctestSuite = (CTestSuite*)(suite);
        QString exeSubdir = project->projectItem()->path().relativePath(ctestSuite->executable().parent());
        //Support for custom RUNTIME_OUTPUT_DIRECTORY target prop is broken
        //QCOMPARE(exeSubdir, ctestSuite->name() == "fail" ? QString("build/bin") : QString("build") );
        QCOMPARE(exeSubdir, ctestSuite->name() == "test_five" ? QString("build/five") : QString("build"));
    }
}

QTEST_MAIN(TestCTestFindSuites)