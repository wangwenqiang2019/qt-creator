/****************************************************************************
**
** Copyright (C) 2016 Brian McGillion and Hugues Delorme
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "vcsbaseclient.h"
#include "vcscommand.h"
#include "vcsbaseclientsettings.h"
#include "vcsbaseeditorconfig.h"

#include <coreplugin/icore.h>
#include <coreplugin/vcsmanager.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/idocument.h>

#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <vcsbase/vcsbaseeditor.h>
#include <vcsbase/vcsoutputwindow.h>
#include <vcsbase/vcsbaseplugin.h>

#include <QStringList>
#include <QDir>
#include <QProcess>
#include <QTextCodec>
#include <QDebug>
#include <QFileInfo>
#include <QByteArray>
#include <QVariant>
#include <QProcessEnvironment>

using namespace Utils;

/*!
    \class VcsBase::VcsBaseClient

    \brief The VcsBaseClient class is the base class for Mercurial and Bazaar
    'clients'.

    Provides base functionality for common commands (diff, log, etc).

    \sa VcsBase::VcsJobRunner
*/

static Core::IEditor *locateEditor(const char *property, const QString &entry)
{
    foreach (Core::IDocument *document, Core::DocumentModel::openedDocuments())
        if (document->property(property).toString() == entry)
            return Core::DocumentModel::editorsForDocument(document).constFirst();
    return nullptr;
}

namespace VcsBase {

VcsBaseClientImpl::VcsBaseClientImpl(VcsBaseSettings *baseSettings)
    : m_baseSettings(baseSettings)
{
    m_baseSettings->readSettings(Core::ICore::settings());
    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
            this, &VcsBaseClientImpl::saveSettings);
}

VcsBaseSettings &VcsBaseClientImpl::settings() const
{
    return *m_baseSettings;
}

FilePath VcsBaseClientImpl::vcsBinary() const
{
    return m_baseSettings->binaryPath.filePath();
}

VcsCommand *VcsBaseClientImpl::createCommand(const FilePath &workingDirectory,
                                             VcsBaseEditorWidget *editor,
                                             JobOutputBindMode mode) const
{
    auto cmd = new VcsCommand(workingDirectory, processEnvironment());
    cmd->setDefaultTimeoutS(vcsTimeoutS());
    if (editor)
        editor->setCommand(cmd);
    if (mode == VcsWindowOutputBind) {
        cmd->addFlags(VcsCommand::ShowStdOut);
        if (editor) // assume that the commands output is the important thing
            cmd->addFlags(VcsCommand::SilentOutput);
    } else if (editor) {
        connect(cmd, &VcsCommand::stdOutText, editor, &VcsBaseEditorWidget::setPlainText);
    }

    return cmd;
}

void VcsBaseClientImpl::enqueueJob(VcsCommand *cmd, const QStringList &args,
                                   const FilePath &workingDirectory,
                                   const ExitCodeInterpreter &interpreter) const
{
    cmd->addJob({vcsBinary(), args}, vcsTimeoutS(), workingDirectory, interpreter);
    cmd->execute();
}

Environment VcsBaseClientImpl::processEnvironment() const
{
    Environment environment = Environment::systemEnvironment();
    VcsBase::setProcessEnvironment(&environment, false);
    return environment;
}

QStringList VcsBaseClientImpl::splitLines(const QString &s)
{
    const QChar newLine = QLatin1Char('\n');
    QString output = s;
    if (output.endsWith(newLine))
        output.truncate(output.size() - 1);
    if (output.isEmpty())
        return QStringList();
    return output.split(newLine);
}

QString VcsBaseClientImpl::stripLastNewline(const QString &in)
{
    if (in.endsWith('\n'))
        return in.left(in.count() - 1);
    return in;
}

void VcsBaseClientImpl::vcsFullySynchronousExec(QtcProcess &proc,
                                                const FilePath &workingDir, const QStringList &args,
                                                unsigned flags, int timeoutS, QTextCodec *codec) const
{
    vcsFullySynchronousExec(proc, workingDir, {vcsBinary(), args}, flags, timeoutS, codec);
}

void VcsBaseClientImpl::vcsFullySynchronousExec(QtcProcess &proc,
                                                const FilePath &workingDir, const CommandLine &cmdLine,
                                                unsigned flags, int timeoutS, QTextCodec *codec) const
{
    VcsCommand command(workingDir, processEnvironment());
    command.addFlags(flags);
    if (codec)
        command.setCodec(codec);
    proc.setTimeoutS(timeoutS > 0 ? timeoutS : vcsTimeoutS());
    command.runCommand(proc, cmdLine);
}

void VcsBaseClientImpl::resetCachedVcsInfo(const FilePath &workingDir)
{
    Core::VcsManager::resetVersionControlForDirectory(workingDir);
}

void VcsBaseClientImpl::annotateRevisionRequested(const FilePath &workingDirectory,
                                                  const QString &file, const QString &change,
                                                  int line)
{
    QString changeCopy = change;
    // This might be invoked with a verbose revision description
    // "SHA1 author subject" from the annotation context menu. Strip the rest.
    const int blankPos = changeCopy.indexOf(QLatin1Char(' '));
    if (blankPos != -1)
        changeCopy.truncate(blankPos);
    annotate(workingDirectory, file, changeCopy, line);
}

VcsCommand *VcsBaseClientImpl::vcsExec(const FilePath &workingDirectory, const QStringList &arguments,
                                       VcsBaseEditorWidget *editor, bool useOutputToWindow,
                                       unsigned additionalFlags, const QVariant &cookie) const
{
    VcsCommand *command = createCommand(workingDirectory, editor,
                                        useOutputToWindow ? VcsWindowOutputBind : NoOutputBind);
    command->setCookie(cookie);
    command->addFlags(additionalFlags);
    if (editor)
        command->setCodec(editor->codec());
    enqueueJob(command, arguments);
    return command;
}

void VcsBaseClientImpl::vcsSynchronousExec(QtcProcess &proc,
                                           const FilePath &workingDir,
                                           const QStringList &args,
                                           unsigned flags,
                                           QTextCodec *outputCodec) const
{
    Environment env = processEnvironment();
    VcsCommand command(workingDir, env.size() == 0 ? Environment::systemEnvironment() : env);
    proc.setTimeoutS(vcsTimeoutS());
    command.addFlags(flags);
    command.setCodec(outputCodec);
    command.runCommand(proc, {vcsBinary(), args});
}

int VcsBaseClientImpl::vcsTimeoutS() const
{
    return m_baseSettings->timeout.value();
}

VcsBaseEditorWidget *VcsBaseClientImpl::createVcsEditor(Utils::Id kind, QString title,
                                                        const QString &source, QTextCodec *codec,
                                                        const char *registerDynamicProperty,
                                                        const QString &dynamicPropertyValue) const
{
    VcsBaseEditorWidget *baseEditor = nullptr;
    Core::IEditor *outputEditor = locateEditor(registerDynamicProperty, dynamicPropertyValue);
    const QString progressMsg = tr("Working...");
    if (outputEditor) {
        // Exists already
        outputEditor->document()->setContents(progressMsg.toUtf8());
        baseEditor = VcsBaseEditor::getVcsBaseEditor(outputEditor);
        QTC_ASSERT(baseEditor, return nullptr);
        Core::EditorManager::activateEditor(outputEditor);
    } else {
        outputEditor = Core::EditorManager::openEditorWithContents(kind, &title, progressMsg.toUtf8());
        outputEditor->document()->setProperty(registerDynamicProperty, dynamicPropertyValue);
        baseEditor = VcsBaseEditor::getVcsBaseEditor(outputEditor);
        QTC_ASSERT(baseEditor, return nullptr);
        connect(baseEditor, &VcsBaseEditorWidget::annotateRevisionRequested,
                this, &VcsBaseClientImpl::annotateRevisionRequested);
        baseEditor->setSource(source);
        if (codec)
            baseEditor->setCodec(codec);
    }

    baseEditor->setForceReadOnly(true);
    return baseEditor;
}

void VcsBaseClientImpl::saveSettings()
{
    m_baseSettings->writeSettings(Core::ICore::settings());
}

VcsBaseClient::VcsBaseClient(VcsBaseSettings *baseSettings)
    : VcsBaseClientImpl(baseSettings)
{
    qRegisterMetaType<QVariant>();
}

bool VcsBaseClient::synchronousCreateRepository(const FilePath &workingDirectory,
                                                const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(CreateRepositoryCommand));
    args << extraOptions;
    QtcProcess proc;
    vcsFullySynchronousExec(proc, workingDirectory, args);
    if (proc.result() != QtcProcess::FinishedWithSuccess)
        return false;
    VcsOutputWindow::append(proc.stdOut());

    resetCachedVcsInfo(workingDirectory);

    return true;
}

bool VcsBaseClient::synchronousClone(const FilePath &workingDir,
                                     const QString &srcLocation,
                                     const QString &dstLocation,
                                     const QStringList &extraOptions)
{
    QStringList args;
    args << vcsCommandString(CloneCommand)
         << extraOptions << srcLocation << dstLocation;

    QtcProcess proc;
    vcsFullySynchronousExec(proc, workingDir, args);
    resetCachedVcsInfo(workingDir);
    return proc.result() == QtcProcess::FinishedWithSuccess;
}

bool VcsBaseClient::synchronousAdd(const FilePath &workingDir,
                                   const QString &relFileName,
                                   const QStringList &extraOptions)
{
    QStringList args;
    args << vcsCommandString(AddCommand) << extraOptions << relFileName;
    QtcProcess proc;
    vcsFullySynchronousExec(proc, workingDir, args);
    return proc.result() == QtcProcess::FinishedWithSuccess;
}

bool VcsBaseClient::synchronousRemove(const FilePath &workingDir,
                                      const QString &filename,
                                      const QStringList &extraOptions)
{
    QStringList args;
    args << vcsCommandString(RemoveCommand) << extraOptions << filename;
    QtcProcess proc;
    vcsFullySynchronousExec(proc, workingDir, args);
    return proc.result() == QtcProcess::FinishedWithSuccess;
}

bool VcsBaseClient::synchronousMove(const FilePath &workingDir,
                                    const QString &from,
                                    const QString &to,
                                    const QStringList &extraOptions)
{
    QStringList args;
    args << vcsCommandString(MoveCommand) << extraOptions << from << to;
    QtcProcess proc;
    vcsFullySynchronousExec(proc, workingDir, args);
    return proc.result() == QtcProcess::FinishedWithSuccess;
}

bool VcsBaseClient::synchronousPull(const FilePath &workingDir,
                                    const QString &srcLocation,
                                    const QStringList &extraOptions)
{
    QStringList args;
    args << vcsCommandString(PullCommand) << extraOptions << srcLocation;
    // Disable UNIX terminals to suppress SSH prompting
    const unsigned flags =
            VcsCommand::SshPasswordPrompt
            | VcsCommand::ShowStdOut
            | VcsCommand::ShowSuccessMessage;
    QtcProcess proc;
    vcsSynchronousExec(proc, workingDir, args, flags);
    const bool ok = proc.result() == QtcProcess::FinishedWithSuccess;
    if (ok)
        emit changed(QVariant(workingDir.toString()));
    return ok;
}

bool VcsBaseClient::synchronousPush(const FilePath &workingDir,
                                    const QString &dstLocation,
                                    const QStringList &extraOptions)
{
    QStringList args;
    args << vcsCommandString(PushCommand) << extraOptions << dstLocation;
    // Disable UNIX terminals to suppress SSH prompting
    const unsigned flags =
            VcsCommand::SshPasswordPrompt
            | VcsCommand::ShowStdOut
            | VcsCommand::ShowSuccessMessage;
    QtcProcess proc;
    vcsSynchronousExec(proc, workingDir, args, flags);
    return proc.result() == QtcProcess::FinishedWithSuccess;
}

VcsBaseEditorWidget *VcsBaseClient::annotate(
        const FilePath &workingDir, const QString &file, const QString &revision /* = QString() */,
        int lineNumber /* = -1 */, const QStringList &extraOptions)
{
    const QString vcsCmdString = vcsCommandString(AnnotateCommand);
    QStringList args;
    args << vcsCmdString << revisionSpec(revision) << extraOptions << file;
    const Utils::Id kind = vcsEditorKind(AnnotateCommand);
    const QString id = VcsBaseEditor::getSource(workingDir, QStringList(file));
    const QString title = vcsEditorTitle(vcsCmdString, id);
    const QString source = VcsBaseEditor::getSource(workingDir, file);

    VcsBaseEditorWidget *editor = createVcsEditor(kind, title, source,
                                                  VcsBaseEditor::getCodec(source),
                                                  vcsCmdString.toLatin1().constData(), id);

    VcsCommand *cmd = createCommand(workingDir, editor);
    cmd->setCookie(lineNumber);
    enqueueJob(cmd, args);
    return editor;
}

void VcsBaseClient::diff(const FilePath &workingDir, const QStringList &files,
                         const QStringList &extraOptions)
{
    const QString vcsCmdString = vcsCommandString(DiffCommand);
    const Utils::Id kind = vcsEditorKind(DiffCommand);
    const QString id = VcsBaseEditor::getTitleId(workingDir, files);
    const QString title = vcsEditorTitle(vcsCmdString, id);
    const QString source = VcsBaseEditor::getSource(workingDir, files);
    VcsBaseEditorWidget *editor = createVcsEditor(kind, title, source,
                                                  VcsBaseEditor::getCodec(source),
                                                  vcsCmdString.toLatin1().constData(), id);
    editor->setWorkingDirectory(workingDir);

    VcsBaseEditorConfig *paramWidget = editor->editorConfig();
    if (!paramWidget) {
        if (m_diffConfigCreator)
            paramWidget = m_diffConfigCreator(editor->toolBar());
        if (paramWidget) {
            paramWidget->setBaseArguments(extraOptions);
            // editor has been just created, createVcsEditor() didn't set a configuration widget yet
            connect(editor, &VcsBaseEditorWidget::diffChunkReverted,
                    paramWidget, &VcsBaseEditorConfig::executeCommand);
            connect(paramWidget, &VcsBaseEditorConfig::commandExecutionRequested,
                [=] { diff(workingDir, files, extraOptions); } );
            editor->setEditorConfig(paramWidget);
        }
    }

    QStringList args = {vcsCmdString};
    if (paramWidget)
        args << paramWidget->arguments();
    else
        args << extraOptions;
    args << files;
    QTextCodec *codec = source.isEmpty() ? static_cast<QTextCodec *>(nullptr)
                                         : VcsBaseEditor::getCodec(source);
    VcsCommand *command = createCommand(workingDir, editor);
    command->setCodec(codec);
    enqueueJob(command, args, workingDir, exitCodeInterpreter(DiffCommand));
}

void VcsBaseClient::log(const FilePath &workingDir,
                        const QStringList &files,
                        const QStringList &extraOptions,
                        bool enableAnnotationContextMenu)
{
    const QString vcsCmdString = vcsCommandString(LogCommand);
    const Utils::Id kind = vcsEditorKind(LogCommand);
    const QString id = VcsBaseEditor::getTitleId(workingDir, files);
    const QString title = vcsEditorTitle(vcsCmdString, id);
    const QString source = VcsBaseEditor::getSource(workingDir, files);
    VcsBaseEditorWidget *editor = createVcsEditor(kind, title, source,
                                                  VcsBaseEditor::getCodec(source),
                                                  vcsCmdString.toLatin1().constData(), id);
    editor->setFileLogAnnotateEnabled(enableAnnotationContextMenu);

    VcsBaseEditorConfig *paramWidget = editor->editorConfig();
    if (!paramWidget) {
        if (m_logConfigCreator)
            paramWidget = m_logConfigCreator(editor->toolBar());
        if (paramWidget) {
            paramWidget->setBaseArguments(extraOptions);
            // editor has been just created, createVcsEditor() didn't set a configuration widget yet
            connect(paramWidget, &VcsBaseEditorConfig::commandExecutionRequested,
                [=] { this->log(workingDir, files, extraOptions, enableAnnotationContextMenu); } );
            editor->setEditorConfig(paramWidget);
        }
    }

    QStringList args = {vcsCmdString};
    if (paramWidget)
        args << paramWidget->arguments();
    else
        args << extraOptions;
    args << files;
    enqueueJob(createCommand(workingDir, editor), args);
}

void VcsBaseClient::revertFile(const FilePath &workingDir,
                               const QString &file,
                               const QString &revision,
                               const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(RevertCommand));
    args << revisionSpec(revision) << extraOptions << file;
    // Indicate repository change or file list
    VcsCommand *cmd = createCommand(workingDir);
    cmd->setCookie(QStringList(workingDir.pathAppended(file).toString()));
    connect(cmd, &VcsCommand::success, this, &VcsBaseClient::changed, Qt::QueuedConnection);
    enqueueJob(cmd, args);
}

void VcsBaseClient::revertAll(const FilePath &workingDir,
                              const QString &revision,
                              const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(RevertCommand));
    args << revisionSpec(revision) << extraOptions;
    // Indicate repository change or file list
    VcsCommand *cmd = createCommand(workingDir);
    cmd->setCookie(QStringList(workingDir.toString()));
    connect(cmd, &VcsCommand::success, this, &VcsBaseClient::changed, Qt::QueuedConnection);
    enqueueJob(createCommand(workingDir), args);
}

void VcsBaseClient::status(const FilePath &workingDir,
                           const QString &file,
                           const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(StatusCommand));
    args << extraOptions << file;
    VcsOutputWindow::setRepository(workingDir.toString());
    VcsCommand *cmd = createCommand(workingDir, nullptr, VcsWindowOutputBind);
    connect(cmd, &VcsCommand::finished,
            VcsOutputWindow::instance(), &VcsOutputWindow::clearRepository,
            Qt::QueuedConnection);
    enqueueJob(cmd, args);
}

void VcsBaseClient::emitParsedStatus(const FilePath &repository, const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(StatusCommand));
    args << extraOptions;
    VcsCommand *cmd = createCommand(repository);
    connect(cmd, &VcsCommand::stdOutText, this, &VcsBaseClient::statusParser);
    enqueueJob(cmd, args);
}

QString VcsBaseClient::vcsCommandString(VcsCommandTag cmd) const
{
    switch (cmd) {
    case CreateRepositoryCommand: return QLatin1String("init");
    case CloneCommand: return QLatin1String("clone");
    case AddCommand: return QLatin1String("add");
    case RemoveCommand: return QLatin1String("remove");
    case MoveCommand: return QLatin1String("rename");
    case PullCommand: return QLatin1String("pull");
    case PushCommand: return QLatin1String("push");
    case CommitCommand: return QLatin1String("commit");
    case ImportCommand: return QLatin1String("import");
    case UpdateCommand: return QLatin1String("update");
    case RevertCommand: return QLatin1String("revert");
    case AnnotateCommand: return QLatin1String("annotate");
    case DiffCommand: return QLatin1String("diff");
    case LogCommand: return QLatin1String("log");
    case StatusCommand: return QLatin1String("status");
    }
    return QString();
}

ExitCodeInterpreter VcsBaseClient::exitCodeInterpreter(VcsCommandTag cmd) const
{
    Q_UNUSED(cmd)
    return {};
}

void VcsBaseClient::setDiffConfigCreator(ConfigCreator creator)
{
    m_diffConfigCreator = std::move(creator);
}

void VcsBaseClient::setLogConfigCreator(ConfigCreator creator)
{
    m_logConfigCreator = std::move(creator);
}

void VcsBaseClient::import(const FilePath &repositoryRoot,
                           const QStringList &files,
                           const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(ImportCommand));
    args << extraOptions << files;
    enqueueJob(createCommand(repositoryRoot), args);
}

void VcsBaseClient::view(const QString &source,
                         const QString &id,
                         const QStringList &extraOptions)
{
    QStringList args;
    args << extraOptions << revisionSpec(id);
    const Utils::Id kind = vcsEditorKind(DiffCommand);
    const QString title = vcsEditorTitle(vcsCommandString(LogCommand), id);

    VcsBaseEditorWidget *editor = createVcsEditor(kind, title, source,
                                                  VcsBaseEditor::getCodec(source), "view", id);

    const QFileInfo fi(source);
    const FilePath workingDirPath = FilePath::fromString(fi.isFile() ? fi.absolutePath() : source);
    enqueueJob(createCommand(workingDirPath, editor), args);
}

void VcsBaseClient::update(const FilePath &repositoryRoot, const QString &revision,
                           const QStringList &extraOptions)
{
    QStringList args(vcsCommandString(UpdateCommand));
    args << revisionSpec(revision) << extraOptions;
    VcsCommand *cmd = createCommand(repositoryRoot);
    cmd->setCookie(repositoryRoot.toString());
    connect(cmd, &VcsCommand::success, this, &VcsBaseClient::changed, Qt::QueuedConnection);
    enqueueJob(cmd, args);
}

void VcsBaseClient::commit(const FilePath &repositoryRoot,
                           const QStringList &files,
                           const QString &commitMessageFile,
                           const QStringList &extraOptions)
{
    // Handling of commitMessageFile is a bit tricky :
    //   VcsBaseClient cannot do something with it because it doesn't know which
    //   option to use (-F ? but sub VCS clients might require a different option
    //   name like -l for hg ...)
    //
    //   So descendants of VcsBaseClient *must* redefine commit() and extend
    //   extraOptions with the usage for commitMessageFile (see BazaarClient::commit()
    //   for example)
    QStringList args(vcsCommandString(CommitCommand));
    args << extraOptions << files;
    VcsCommand *cmd = createCommand(repositoryRoot, nullptr, VcsWindowOutputBind);
    if (!commitMessageFile.isEmpty())
        connect(cmd, &VcsCommand::finished, [commitMessageFile]() { QFile(commitMessageFile).remove(); });
    enqueueJob(cmd, args);
}

QString VcsBaseClient::vcsEditorTitle(const QString &vcsCmd, const QString &sourceId) const
{
    return vcsBinary().baseName() + QLatin1Char(' ') + vcsCmd + QLatin1Char(' ')
           + FilePath::fromString(sourceId).fileName();
}

void VcsBaseClient::statusParser(const QString &text)
{
    QList<VcsBaseClient::StatusItem> lineInfoList;

    QStringList rawStatusList = text.split(QLatin1Char('\n'));

    foreach (const QString &string, rawStatusList) {
        const VcsBaseClient::StatusItem lineInfo = parseStatusLine(string);
        if (!lineInfo.flags.isEmpty() && !lineInfo.file.isEmpty())
            lineInfoList.append(lineInfo);
    }

    emit parsedStatus(lineInfoList);
}

} // namespace VcsBase

#include "moc_vcsbaseclient.cpp"
