/*
 *
 */
#include "testgenerationtest.h"
#include "codeutils_tests_config.h"

#include <tests/testcore.h>
#include <tests/autotestshell.h>
#include <language/codegen/templatesmodel.h>
#include <language/codegen/sourcefiletemplate.h>
#include <language/codegen/documentchangeset.h>
#include <language/codegen/templaterenderer.h>

#include <KStandardDirs>
#include <qtest_kde.h>

using namespace KDevelop;


#define COMPARE_FILES(name)                                                 \
do {                                                                        \
KUrl resultUrl(baseUrl);                                                    \
resultUrl.addPath(name);                                                    \
QFile actualFile(resultUrl.toLocalFile());                                  \
QVERIFY(actualFile.open(QIODevice::ReadOnly));                              \
QFile expectedFile(CODEUTILS_TESTS_EXPECTED_DIR "/" name);                  \
QVERIFY(expectedFile.open(QIODevice::ReadOnly));                            \
QCOMPARE(actualFile.size(), expectedFile.size());                           \
QCOMPARE(QString(actualFile.readAll()), QString(expectedFile.readAll()));   \
} while(0)

void TestGenerationTest::initTestCase()
{
    AutoTestShell::init();
    TestCore::initialize (Core::NoUi);

    KComponentData data = ICore::self()->componentData();
    baseUrl.setDirectory(KStandardDirs::locateLocal("tmp", "test_templatetestgenerator/", data));

    KStandardDirs *dirs = data.dirs();
    bool addedDir = dirs->addResourceDir("filetemplates", CODEUTILS_TESTS_TEMPLATES_DIR, true);
    QVERIFY(addedDir);

    TemplatesModel model(data, this);
    model.setTemplateResourceType("filetemplates");
    model.setDescriptionResourceType("filetemplate_descriptions");
    model.refresh();

    renderer = new TemplateRenderer;
    renderer->setEmptyLinesPolicy(TemplateRenderer::TrimEmptyLines);
    renderer->addVariable("name", "TestName");
    renderer->addVariable("license", "Test license header\nIn two lines");

    QStringList testCases;
    testCases << "firstTestCase";
    testCases << "secondTestCase";
    testCases << "thirdTestCase";
    renderer->addVariable("testCases", testCases);
}

void TestGenerationTest::cleanupTestCase()
{
    delete renderer;
    qDebug() << "Shutting down";
    TestCore::shutdown();
    qDebug() << "Core is finished";
}

void TestGenerationTest::init()
{
    QDir dir(baseUrl.toLocalFile());
    foreach (const QString& fileName, dir.entryList(QDir::Files | QDir::NoDotAndDotDot))
    {
        dir.remove(fileName);
    }
}

void TestGenerationTest::yamlTemplate()
{
    QString description = ICore::self()->componentData().dirs()->findResource("filetemplate_descriptions", "test_yaml.desktop");
    QVERIFY(!description.isEmpty());
    SourceFileTemplate file(description);
    QCOMPARE(file.name(), QString("Testing YAML Template"));

    DocumentChangeSet changes = renderer->renderFileTemplate(&file, baseUrl, urls(file));
    changes.applyAllChanges();

    COMPARE_FILES("testname.yaml");
}

void TestGenerationTest::cppTemplate()
{
    QString description = ICore::self()->componentData().dirs()->findResource("filetemplate_descriptions", "test_qtestlib.desktop");
    QVERIFY(!description.isEmpty());
    SourceFileTemplate file(description);

    QCOMPARE(file.name(), QString("Testing C++ Template"));

    DocumentChangeSet changes = renderer->renderFileTemplate(&file, baseUrl, urls(file));
    changes.applyAllChanges();

    COMPARE_FILES("testname.h");
    COMPARE_FILES("testname.cpp");
}

QHash< QString, KUrl > TestGenerationTest::urls (const SourceFileTemplate& file)
{
    QHash<QString, KUrl> ret;
    foreach (const SourceFileTemplate::OutputFile& output, file.outputFiles())
    {
        KUrl url(baseUrl);
        url.addPath(renderer->render(output.outputName).toLower());
        ret.insert(output.identifier, url);
    }
    return ret;
}


QTEST_KDEMAIN (TestGenerationTest, NoGUI);
