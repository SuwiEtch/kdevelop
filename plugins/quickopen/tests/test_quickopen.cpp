/*
 * Copyright <year> Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "test_quickopen.h"
#include <interfaces/idocumentcontroller.h>

#include <KTempDir>
#include <QtTest/QTest>
#include <QTemporaryFile>

QTEST_MAIN(TestQuickOpen);

using namespace KDevelop;

TestQuickOpen::TestQuickOpen(QObject* parent)
: QuickOpenTestBase(Core::Default, parent)
{

}

void TestQuickOpen::testDuchainFilter()
{
    using ItemList = QList<DUChainItem>;

    QFETCH(ItemList, items);
    QFETCH(QString, filter);
    QFETCH(ItemList, filtered);

    auto toStringList = [](const ItemList& items) {
        QStringList result;
        for ( const DUChainItem& item: items ) {
            result << item.m_text;
        }
        return result;
    };

    TestFilter filterItems;
    filterItems.setItems(items);
    filterItems.setFilter(filter);
    QCOMPARE(toStringList(filterItems.filteredItems()), toStringList(filtered));
}

void TestQuickOpen::testDuchainFilter_data()
{
    using ItemList = QList<DUChainItem>;

    QTest::addColumn<ItemList>("items");
    QTest::addColumn<QString>("filter");
    QTest::addColumn<ItemList>("filtered");

    auto i = [](const QString& text) {
        auto item = DUChainItem();
        item.m_text = text;
        return item;
    };

    auto items = ItemList()
        << i("KTextEditor::Cursor")
        << i("void KTextEditor::Cursor::explode()")
        << i("QVector<int> SomeNamespace::SomeClass::func(int)");

    QTest::newRow("prefix") << items << "KTE" << ( ItemList() << items.at(0) << items.at(1) );
    QTest::newRow("prefix_mismatch") << items << "KTEY" << ( ItemList() );
    QTest::newRow("prefix_colon") << items << "KTE:" << ( ItemList() << items.at(0) << items.at(1) );
    QTest::newRow("prefix_colon_mismatch") << items << "KTE:Y" << ( ItemList() );
    QTest::newRow("prefix_colon_mismatch2") << items << "XKTE:" << ( ItemList() );
    QTest::newRow("prefix_two_colon") << items << "KTE::" << ( ItemList() << items.at(0) << items.at(1) );
    QTest::newRow("prefix_two_colon_mismatch") << items << "KTE::Y" << ( ItemList() );
    QTest::newRow("prefix_two_colon_mismatch2") << items << "XKTE::" << ( ItemList() );
    QTest::newRow("suffix") << items << "Curs" << ( ItemList() << items.at(0) << items.at(1) );
    QTest::newRow("suffix2") << items << "curs" << ( ItemList() << items.at(0) << items.at(1) );
    QTest::newRow("mid") << items << "SomeClass" << ( ItemList() << items.at(2) );
    QTest::newRow("mid_abbrev") << items << "SClass" << ( ItemList() << items.at(2) );
}

void TestQuickOpen::testAbbreviations()
{
    QFETCH(QStringList, items);
    QFETCH(QString, filter);
    QFETCH(QStringList, filtered);

    PathTestFilter filterItems;
    filterItems.setItems(items);
    filterItems.setFilter(filter.split('/', QString::SkipEmptyParts));
    QCOMPARE(QStringList(filterItems.filteredItems()), filtered);
}

void TestQuickOpen::testAbbreviations_data()
{
    QTest::addColumn<QStringList>("items");
    QTest::addColumn<QString>("filter");
    QTest::addColumn<QStringList>("filtered");

    const QStringList items = QStringList()
        << "/foo/bar/caz/a.h"
        << "/KateThing/CMakeLists.txt"
        << "/FooBar/FooBar/Footestfoo.h";

    QTest::newRow("path_segments") << items << "fbc" << ( QStringList() );
    QTest::newRow("path_segment_abbrev") << items << "cmli" << ( QStringList() << items.at(1) );
    QTest::newRow("path_segment_old") << items << "kate/cmake" << ( QStringList() << items.at(1) );
    QTest::newRow("path_segment_multi_mixed") << items << "ftfoo.h" << ( QStringList() << items.at(2) );
}

void TestQuickOpen::testSorting()
{
    QFETCH(QStringList, items);
    QFETCH(QString, filter);
    QFETCH(QStringList, filtered);

    PathTestFilter filterItems;
    filterItems.setItems(items);
    filterItems.setFilter(filter.split('/', QString::SkipEmptyParts));
    QEXPECT_FAIL("bar7", "empty parts are skipped", Continue);
    QCOMPARE(QStringList(filterItems.filteredItems()), filtered);
}

void TestQuickOpen::testSorting_data()
{
    QTest::addColumn<QStringList>("items");
    QTest::addColumn<QString>("filter");
    QTest::addColumn<QStringList>("filtered");

    const QStringList items = QStringList()
        << "/foo/a.h"
        << "/foo/ab.h"
        << "/foo/bc.h"
        << "/bar/a.h";

    {
        QTest::newRow("no-filter") << items << QString() << items;
    }
    {
        const QStringList filtered = QStringList() << "/bar/a.h";
        QTest::newRow("bar1") << items << QString("bar") << filtered;
        QTest::newRow("bar2") << items << QString("/bar") << filtered;
        QTest::newRow("bar3") << items << QString("/bar/") << filtered;
        QTest::newRow("bar4") << items << QString("bar/") << filtered;
        QTest::newRow("bar5") << items << QString("ar/") << filtered;
        QTest::newRow("bar6") << items << QString("r/") << filtered;
        QTest::newRow("bar7") << items << QString("b/") << filtered;
        QTest::newRow("bar8") << items << QString("b/a") << filtered;
        QTest::newRow("bar9") << items << QString("b/a.h") << filtered;
        QTest::newRow("bar10") << items << QString("b/a.") << filtered;
    }
    {
        const QStringList filtered = QStringList() << "/foo/a.h" << "/foo/ab.h";
        QTest::newRow("foo_a1") << items << QString("foo/a") << filtered;
        QTest::newRow("foo_a2") << items << QString("/f/a") << filtered;
    }
    {
        // now matches ab.h too because of abbreviation matching, but should be sorted last
        const QStringList filtered = QStringList() << "/foo/a.h" << "/bar/a.h" << "/foo/ab.h";
        QTest::newRow("a_h") << items << QString("a.h") << filtered;
    }
    {
        const QStringList base = QStringList() << "/foo/a_test" << "/foo/test_b_1" << "/foo/test_b";
        const QStringList sorted = QStringList() << "/foo/test_b" << "/foo/test_b_1";
        QTest::newRow("prefer_exact") << base << QString("test_b") << sorted;
    }
    {
        // from commit: 769491f06a4560a4798592ff060675ffb0d990a6
        const QString file = "/myProject/someStrangePath/anItem.cpp";
        const QStringList base = QStringList() << "/foo/a" << file;
        const QStringList filtered = QStringList() << file;
        QTest::newRow("strange") << base << QString("strange/item") << filtered;
    }
    {
        const QStringList base = QStringList() << "/foo/a_test" << "/foo/test_b_1"
                                               << "/foo/test_b" << "/foo/test/a";
        const QStringList sorted = QStringList() << "/foo/test_b_1" << "/foo/test_b"
                                                 << "/foo/a_test" << "/foo/test/a";
        QTest::newRow("prefer_start1") << base << QString("test") << sorted;
        QTest::newRow("prefer_start2") << base << QString("foo/test") << sorted;
    }
    {
        const QStringList base = QStringList() << "/muh/kuh/asdf/foo" << "/muh/kuh/foo/asdf";
        const QStringList reverse = QStringList() << "/muh/kuh/foo/asdf" << "/muh/kuh/asdf/foo";
        QTest::newRow("prefer_start3") << base << QString("f") << base;
        QTest::newRow("prefer_start4") << base << QString("/fo") << base;
        QTest::newRow("prefer_start5") << base << QString("/foo") << base;
        QTest::newRow("prefer_start6") << base << QString("a") << reverse;
        QTest::newRow("prefer_start7") << base << QString("/a") << reverse;
        QTest::newRow("prefer_start8") << base << QString("uh/as") << reverse;
        QTest::newRow("prefer_start9") << base << QString("asdf") << reverse;
    }
    {
        QTest::newRow("duplicate") << (QStringList() << "/muh/kuh/asdf/foo") << QString("kuh/kuh") << QStringList();
    }
}

void TestQuickOpen::testProjectFileFilter()
{
    KTempDir dir;
    TestProject* project = new TestProject(Path(dir.name()));
    ProjectFolderItem* foo = createChild<ProjectFolderItem>(project->projectItem(), "foo");
    createChild<ProjectFileItem>(foo, "bar");
    createChild<ProjectFileItem>(foo, "asdf");
    createChild<ProjectFileItem>(foo, "space bar");
    ProjectFolderItem* asdf = createChild<ProjectFolderItem>(project->projectItem(), "asdf");
    createChild<ProjectFileItem>(asdf, "bar");

    QTemporaryFile tmpFile;
    tmpFile.setFileName(dir.name() + "aaaa");
    QVERIFY(tmpFile.open());
    ProjectFileItem* aaaa = new ProjectFileItem("aaaa", project->projectItem());
    QCOMPARE(project->fileSet().size(), 5);

    ProjectFileDataProvider provider;
    QCOMPARE(provider.itemCount(), 0u);
    projectController->addProject(project);

    const QStringList original = QStringList()
        << "aaaa" << "asdf/bar" << "foo/asdf" << "foo/bar" << "foo/space bar";

    // lazy load
    QCOMPARE(provider.itemCount(), 0u);
    provider.reset();
    QCOMPARE(items(provider), original);

    QCOMPARE(provider.itemPath(provider.items().first()), aaaa->path());
    QCOMPARE(provider.data(0)->text(), QString("aaaa"));

    // don't show opened file
    QVERIFY(core->documentController()->openDocument(QUrl::fromLocalFile(tmpFile.fileName())));
    // lazy load again
    QCOMPARE(items(provider), original);
    provider.reset();
    QCOMPARE(items(provider), QStringList() << "asdf/bar" << "foo/asdf" << "foo/bar" << "foo/space bar");

    // prefer files starting with filter
    provider.setFilterText("as");
    qDebug() << items(provider);
    QCOMPARE(items(provider), QStringList() << "foo/asdf" << "asdf/bar");

    // clear filter
    provider.reset();
    QCOMPARE(items(provider), QStringList() << "asdf/bar" << "foo/asdf" << "foo/bar" << "foo/space bar");

    // update on document close, lazy load again
    core->documentController()->closeAllDocuments();
    QCOMPARE(items(provider), QStringList() << "asdf/bar" << "foo/asdf" << "foo/bar" << "foo/space bar");
    provider.reset();
    QCOMPARE(items(provider), original);

    ProjectFileItem* blub = createChild<ProjectFileItem>(project->projectItem(), "blub");
    // lazy load
    QCOMPARE(provider.itemCount(), 5u);
    provider.reset();
    QCOMPARE(provider.itemCount(), 6u);

    // ensure we don't add stuff multiple times
    QMetaObject::invokeMethod(&provider, "fileAddedToSet",
                              Q_ARG(KDevelop::IProject*, project),
                              Q_ARG(KDevelop::IndexedString, blub->indexedPath()));
    QCOMPARE(provider.itemCount(), 6u);
    provider.reset();
    QCOMPARE(provider.itemCount(), 6u);

    // lazy load in this implementation
    delete blub;
    QCOMPARE(provider.itemCount(), 6u);
    provider.reset();
    QCOMPARE(provider.itemCount(), 5u);

    QCOMPARE(items(provider), original);

    // allow filtering by path to project
    provider.setFilterText(dir.name());
    QCOMPARE(items(provider), original);

    Path buildFolderItem(project->path().parent(), ".build/generated.h");
    new ProjectFileItem(project, buildFolderItem, project->projectItem());
    // lazy load
    QCOMPARE(items(provider), original);
    provider.reset();
    QCOMPARE(items(provider), QStringList() << "aaaa" << "asdf/bar" << "foo/asdf"
                                            << "foo/bar" << "foo/space bar" << "../.build/generated.h");

    projectController->closeProject(project);
    provider.reset();
    QVERIFY(!provider.itemCount());
}
