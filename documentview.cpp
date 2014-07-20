#include "documentview.h"
#include "ui_documentview.h"

#include <QFileDialog>
#include <QFileSystemModel>
#include <QStringListModel>

#include <QTemporaryFile>
#include <QProcess>

#include <QtDebug>

#include "targetupdatediscover.h"

static const QStringList PROJECT_FILES = QString("*.c *.cpp *.h *.hpp *.cc *.hh Makefile").split(' ');

DocumentView::DocumentView(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DocumentView)
{
    buildProc = new QProcess(this);
    buildProc->setObjectName("buildProc");
    connect(buildProc, SIGNAL(finished(int)), this, SIGNAL(buildEnd(int)));
    ui->setupUi(this);
}

DocumentView::~DocumentView()
{
    delete ui;
}

QString DocumentView::project() const
{
    QFileSystemModel *m = qobject_cast<QFileSystemModel*>(ui->treeView->model());
    if (!m)
        return QString();
    return m->rootDirectory().absoluteFilePath("Makefile");
}

void DocumentView::closeProject()
{
    if (ui->treeView->model()) {
        ui->treeView->model()->deleteLater();
        ui->treeView->setModel(0l);
    }
    ui->targetList->clear();
}

void DocumentView::openProject(const QString &projectFile)
{
    if (!projectFile.isEmpty()) {
        QFileInfo mk(projectFile);
        QFileSystemModel *model = new QFileSystemModel(this);
        model->setFilter(QDir::AllDirs|QDir::NoDotAndDotDot|QDir::Files);
        model->setNameFilterDisables(false);
        model->setNameFilters(PROJECT_FILES);
        ui->treeView->model()->deleteLater();
        ui->treeView->setModel(model);
        ui->treeView->setRootIndex(model->setRootPath(mk.path()));
        for(int i=2; i<model->columnCount(); i++)
            ui->treeView->hideColumn(i);
#if QT_VERSION >= 0x050000
        ui->treeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#else
        ui->treeView->header()->setResizeMode(0, QHeaderView::ResizeToContents);
#endif
        (new TargetUpdateDiscover(this, SLOT(updateTargets(QStringList))))->start(project());
    }
}

static QString tmpName() {
    QTemporaryFile f("empty-XXXXXX");
    f.open();
    QString s = QFileInfo(f).fileName();
    f.remove();
    return s;
}

static const QString EXCLUDE_LIST = QString(
        "-x .git* "
        "-x *.o "
        "-x *.d "
        "-x *.elf "
        "-x *.lst "
        "-x *.map "
        "-x .config* "
        "-x *.conf "
        "-x autoconf.h");

QString DocumentView::makeTemplate(const QString &diffFile)
{
    // FIXME Esto tampoco tiene porque estar aca! Pasale el proyecto a otro objeto y fue!
    QFileSystemModel *m = qobject_cast<QFileSystemModel*>(ui->treeView->model());
    if (!m)
        return tr("No project");
    QString error = tr("Unknow error");
    QDir projectRoot = m->rootDirectory();
    QString tmpDirName = QDir::tempPath() + QDir::separator() + tmpName();
    if (QDir::root().mkdir(tmpDirName)) {
        QProcess diff;
        diff.setWorkingDirectory(projectRoot.absolutePath());
        diff.start(QString("diff -Naur %2 %1 .").arg(tmpDirName).arg(EXCLUDE_LIST));
        if (diff.waitForStarted(3000)) {
            if (diff.waitForFinished(3000)) {
                QString pathText = diff.readAll();
                QFile out(diffFile);
                if (out.open(QFile::WriteOnly)) {
                    out.write(pathText.toLocal8Bit());
                    error = QString();
                } else
                    error = tr("Cant create template file");
            } else
                error = tr("Diff cant finish normaly");
        } else
            error = tr("Diff cant start");
        QDir::root().rmdir(tmpDirName);
    } else
        error = tr("Cant create empty tmp dir");
    return error;
}

void DocumentView::buildStart(const QString &target)
{
    if (buildProc->state() != QProcess::NotRunning)
        buildStop();
    buildProc->setWorkingDirectory(QFileInfo(project()).absoluteDir().absolutePath());
    buildProc->start(QString("make -f %1 %2")
                     .arg(project())
                     .arg(target));
}

void DocumentView::buildStop()
{
    if (buildProc->state() != QProcess::NotRunning) {
        buildProc->terminate();
        if (!buildProc->waitForFinished(1000)) {
            emit buildStderr(tr("Killing process"));
            buildProc->kill();
        }
    }
}

void DocumentView::on_treeView_activated(const QModelIndex &index)
{
    QFileSystemModel *m = qobject_cast<QFileSystemModel*>(ui->treeView->model());
    if (m) {
        QFileInfo f = m->fileInfo(index);
        if (f.isFile())
            emit fileOpen(f.filePath());
    }
}

void DocumentView::updateTargets(const QStringList &targetList)
{
    QIcon icon = QIcon::fromTheme("run-build-configure");
    ui->targetList->clear();
    foreach(QString t, targetList)
        ui->targetList->addItem(new QListWidgetItem(icon, t));
}

void DocumentView::on_targetList_doubleClicked(const QModelIndex &index)
{
    QListWidgetItem *item = ui->targetList->item(index.row());
    emit startBuild(item->text());
}

void DocumentView::on_buildProc_readyReadStandardError()
{
    emit buildStderr(buildProc->readAllStandardError());
}

void DocumentView::on_buildProc_readyReadStandardOutput()
{
    emit buildStdout(buildProc->readAllStandardOutput());
}
