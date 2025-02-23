/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "documentmanager.h"
#include "qmldesignerplugin.h"

#include <modelnode.h>
#include <qmlitemnode.h>
#include <nodemetainfo.h>
#include <nodeproperty.h>
#include <nodelistproperty.h>
#include <bindingproperty.h>
#include <variantproperty.h>

#include <utils/qtcassert.h>
#include <utils/textfileformat.h>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/iversioncontrol.h>
#include <coreplugin/vcsmanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagebox.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/session.h>

#include <qmakeprojectmanager/qmakenodes.h>
#include <qmakeprojectmanager/qmakeproject.h>

#include <QMessageBox>

using namespace Utils;

namespace QmlDesigner {

Q_LOGGING_CATEGORY(documentManagerLog, "qtc.qtquickdesigner.documentmanager", QtWarningMsg)

static inline QmlDesigner::DesignDocument* designDocument()
{
    return QmlDesigner::QmlDesignerPlugin::instance()->documentManager().currentDesignDocument();
}

static inline QHash<PropertyName, QVariant> getProperties(const ModelNode &node)
{
    QHash<PropertyName, QVariant> propertyHash;
    if (QmlObjectNode::isValidQmlObjectNode(node)) {
        foreach (const AbstractProperty &abstractProperty, node.properties()) {
            if (abstractProperty.isVariantProperty()
                    || (abstractProperty.isBindingProperty()
                        && !abstractProperty.name().contains("anchors.")))
                propertyHash.insert(abstractProperty.name(), QmlObjectNode(node).instanceValue(abstractProperty.name()));
        }

        if (QmlItemNode::isValidQmlItemNode(node)) {
            QmlItemNode itemNode(node);

            propertyHash.insert("width", itemNode.instanceValue("width"));
            propertyHash.insert("height", itemNode.instanceValue("height"));
            propertyHash.remove("x");
            propertyHash.remove("y");
            propertyHash.remove("rotation");
            propertyHash.remove("opacity");
        }
    }

    return propertyHash;
}

static inline void applyProperties(ModelNode &node, const QHash<PropertyName, QVariant> &propertyHash)
{
    QHash<PropertyName, QVariant> auxiliaryData  = node.auxiliaryData();

    foreach (const PropertyName &propertyName, auxiliaryData.keys()) {
        if (node.hasAuxiliaryData(propertyName))
            node.setAuxiliaryData(propertyName, QVariant());
    }

    for (auto propertyIterator = propertyHash.cbegin(), end = propertyHash.cend();
              propertyIterator != end;
              ++propertyIterator) {
        const PropertyName propertyName = propertyIterator.key();
        if (propertyName == "width" || propertyName == "height") {
            node.setAuxiliaryData(propertyIterator.key(), propertyIterator.value());
        }
    }
}

static void openFileComponent(const ModelNode &modelNode)
{
    QmlDesignerPlugin::instance()->viewManager().nextFileIsCalledInternally();
    Core::EditorManager::openEditor(modelNode.metaInfo().componentFileName(),
        Utils::Id(), Core::EditorManager::DoNotMakeVisible);
}

static void openFileComponentForDelegate(const ModelNode &modelNode)
{
    openFileComponent(modelNode.nodeProperty("delegate").modelNode());
}

static void openComponentSourcePropertyOfLoader(const ModelNode &modelNode)
{
    QmlDesignerPlugin::instance()->viewManager().nextFileIsCalledInternally();

    ModelNode componentModelNode;

    if (modelNode.hasNodeProperty("sourceComponent")) {
        componentModelNode = modelNode.nodeProperty("sourceComponent").modelNode();
    } else if (modelNode.hasNodeListProperty("component")) {

     /*
     * The component property should be a NodeProperty, but currently is a NodeListProperty, because
     * the default property is always implcitly a NodeListProperty. This is something that has to be fixed.
     */

        componentModelNode = modelNode.nodeListProperty("component").toModelNodeList().constFirst();
    }

    Core::EditorManager::openEditor(componentModelNode.metaInfo().componentFileName(), Utils::Id(), Core::EditorManager::DoNotMakeVisible);
}

static void openSourcePropertyOfLoader(const ModelNode &modelNode)
{
    QmlDesignerPlugin::instance()->viewManager().nextFileIsCalledInternally();

    QString componentFileName = modelNode.variantProperty("source").value().toString();

    QFileInfo fileInfo(modelNode.model()->fileUrl().toLocalFile());
    Core::EditorManager::openEditor(fileInfo.absolutePath() + "/" + componentFileName, Utils::Id(), Core::EditorManager::DoNotMakeVisible);
}


static void handleComponent(const ModelNode &modelNode)
{
    if (modelNode.nodeSourceType() == ModelNode::NodeWithComponentSource)
        designDocument()->changeToSubComponent(modelNode);
}

static void handleDelegate(const ModelNode &modelNode)
{
    if (modelNode.metaInfo().isView()
            && modelNode.hasNodeProperty("delegate")
            && modelNode.nodeProperty("delegate").modelNode().nodeSourceType() == ModelNode::NodeWithComponentSource)
        designDocument()->changeToSubComponent(modelNode.nodeProperty("delegate").modelNode());
}

static void handleTabComponent(const ModelNode &modelNode)
{
    if (modelNode.hasNodeProperty("component")
            && modelNode.nodeProperty("component").modelNode().nodeSourceType() == ModelNode::NodeWithComponentSource) {
        designDocument()->changeToSubComponent(modelNode.nodeProperty("component").modelNode());
    }
}

static inline void openInlineComponent(const ModelNode &modelNode)
{
    if (!modelNode.metaInfo().isValid())
        return;

    handleComponent(modelNode);
    handleDelegate(modelNode);
    handleTabComponent(modelNode);
}

static bool isFileComponent(const ModelNode &node)
{
    if (node.isValid()
            && node.metaInfo().isValid()
            && node.metaInfo().isFileComponent())
        return true;

    return false;
}

static bool hasDelegateWithFileComponent(const ModelNode &node)
{
    if (node.isValid()
            && node.metaInfo().isValid()
            && node.metaInfo().isView()
            && node.hasNodeProperty("delegate")
            && node.nodeProperty("delegate").modelNode().metaInfo().isFileComponent())
        return true;

    return false;
}

static bool isLoaderWithSourceComponent(const ModelNode &modelNode)
{
    if (modelNode.isValid()
            && modelNode.metaInfo().isValid()
            && modelNode.metaInfo().isSubclassOf("QtQuick.Loader")) {

        if (modelNode.hasNodeProperty("sourceComponent"))
            return true;
        if (modelNode.hasNodeListProperty("component"))
            return true;
    }

    return false;
}

static bool hasSourceWithFileComponent(const ModelNode &modelNode)
{
    if (modelNode.isValid()
            && modelNode.metaInfo().isValid()
            && modelNode.metaInfo().isSubclassOf("QtQuick.Loader")
            && modelNode.hasVariantProperty("source"))
        return true;

    return false;
}

DocumentManager::DocumentManager()
    : QObject()
{
}

DocumentManager::~DocumentManager()
{
    qDeleteAll(m_designDocumentHash);
}

void DocumentManager::setCurrentDesignDocument(Core::IEditor *editor)
{
    if (editor) {
        m_currentDesignDocument = m_designDocumentHash.value(editor);
        if (m_currentDesignDocument == nullptr) {
            m_currentDesignDocument = new DesignDocument;
            m_designDocumentHash.insert(editor, m_currentDesignDocument);
            m_currentDesignDocument->setEditor(editor);
        }
    } else if (!m_currentDesignDocument.isNull()) {
        m_currentDesignDocument->resetToDocumentModel();
        m_currentDesignDocument.clear();
    }
}

DesignDocument *DocumentManager::currentDesignDocument() const
{
    return m_currentDesignDocument.data();
}

bool DocumentManager::hasCurrentDesignDocument() const
{
    return !m_currentDesignDocument.isNull();
}

void DocumentManager::removeEditors(const QList<Core::IEditor *> &editors)
{
    foreach (Core::IEditor *editor, editors)
        delete m_designDocumentHash.take(editor).data();
}

bool DocumentManager::goIntoComponent(const ModelNode &modelNode)
{
    if (modelNode.isValid() && modelNode.isComponent() && designDocument()) {
        QmlDesignerPlugin::instance()->viewManager().setComponentNode(modelNode);
        QHash<PropertyName, QVariant> oldProperties = getProperties(modelNode);
        if (isFileComponent(modelNode))
            openFileComponent(modelNode);
        else if (hasDelegateWithFileComponent(modelNode))
            openFileComponentForDelegate(modelNode);
        else if (hasSourceWithFileComponent(modelNode))
            openSourcePropertyOfLoader(modelNode);
        else if (isLoaderWithSourceComponent(modelNode))
            openComponentSourcePropertyOfLoader(modelNode);
        else
            openInlineComponent(modelNode);

        ModelNode rootModelNode = designDocument()->rewriterView()->rootModelNode();
        applyProperties(rootModelNode, oldProperties);

        return true;
    }

    return false;
}

bool DocumentManager::createFile(const QString &filePath, const QString &contents)
{
    Utils::TextFileFormat textFileFormat;
    textFileFormat.codec = Core::EditorManager::defaultTextCodec();
    QString errorMessage;

    return textFileFormat.writeFile(Utils::FilePath::fromString(filePath), contents, &errorMessage);
}

void DocumentManager::addFileToVersionControl(const QString &directoryPath, const QString &newFilePath)
{
    Core::IVersionControl *versionControl =
        Core::VcsManager::findVersionControlForDirectory(FilePath::fromString(directoryPath));
    if (versionControl && versionControl->supportsOperation(Core::IVersionControl::AddOperation)) {
        const QMessageBox::StandardButton button
            = QMessageBox::question(Core::ICore::dialogParent(),
                                    Core::VcsManager::msgAddToVcsTitle(),
                                    Core::VcsManager::msgPromptToAddToVcs(QStringList(newFilePath),
                                                                          versionControl),
                                    QMessageBox::Yes | QMessageBox::No);
        if (button == QMessageBox::Yes && !versionControl->vcsAdd(FilePath::fromString(newFilePath))) {
            Core::AsynchronousMessageBox::warning(Core::VcsManager::msgAddToVcsFailedTitle(),
                                                   Core::VcsManager::msgToAddToVcsFailed(QStringList(newFilePath), versionControl));
        }
    }
}

Utils::FilePath DocumentManager::currentFilePath()
{
    if (!QmlDesignerPlugin::instance()->currentDesignDocument())
        return {};

    return QmlDesignerPlugin::instance()->documentManager().currentDesignDocument()->fileName();
}

Utils::FilePath DocumentManager::currentProjectDirPath()
{
    QTC_ASSERT(QmlDesignerPlugin::instance(), return {});

    if (!QmlDesignerPlugin::instance()->currentDesignDocument())
        return {};

    Utils::FilePath qmlFileName = QmlDesignerPlugin::instance()->currentDesignDocument()->fileName();
    ProjectExplorer::Project *project = ProjectExplorer::SessionManager::projectForFile(qmlFileName);
    if (!project)
        return {};

    return project->projectDirectory();
}

QStringList DocumentManager::isoIconsQmakeVariableValue(const QString &proPath)
{
    ProjectExplorer::Node *node = ProjectExplorer::ProjectTree::nodeForFile(Utils::FilePath::fromString(proPath));
    if (!node) {
        qCWarning(documentManagerLog) << "No node for .pro:" << proPath;
        return QStringList();
    }

    ProjectExplorer::Node *parentNode = node->parentFolderNode();
    if (!parentNode) {
        qCWarning(documentManagerLog) << "No parent node for node at" << proPath;
        return QStringList();
    }

    auto proNode = dynamic_cast<QmakeProjectManager::QmakeProFileNode*>(parentNode);
    if (!proNode) {
        qCWarning(documentManagerLog) << "Parent node for node at" << proPath << "could not be converted to a QmakeProFileNode";
        return QStringList();
    }

    return proNode->variableValue(QmakeProjectManager::Variable::IsoIcons);
}

bool DocumentManager::setIsoIconsQmakeVariableValue(const QString &proPath, const QStringList &value)
{
    ProjectExplorer::Node *node = ProjectExplorer::ProjectTree::nodeForFile(Utils::FilePath::fromString(proPath));
    if (!node) {
        qCWarning(documentManagerLog) << "No node for .pro:" << proPath;
        return false;
    }

    ProjectExplorer::Node *parentNode = node->parentFolderNode();
    if (!parentNode) {
        qCWarning(documentManagerLog) << "No parent node for node at" << proPath;
        return false;
    }

    auto proNode = dynamic_cast<QmakeProjectManager::QmakeProFileNode*>(parentNode);
    if (!proNode) {
        qCWarning(documentManagerLog) << "Node for" << proPath << "could not be converted to a QmakeProFileNode";
        return false;
    }

    int flags = QmakeProjectManager::Internal::ProWriter::ReplaceValues | QmakeProjectManager::Internal::ProWriter::MultiLine;
    QmakeProjectManager::QmakeProFile *pro = proNode->proFile();
    if (pro)
        return pro->setProVariable("ISO_ICONS", value, QString(), flags);
    return false;
}

void DocumentManager::findPathToIsoProFile(bool *iconResourceFileAlreadyExists, QString *resourceFilePath,
    QString *resourceFileProPath, const QString &isoIconsQrcFile)
{
    Utils::FilePath qmlFileName = QmlDesignerPlugin::instance()->currentDesignDocument()->fileName();
    ProjectExplorer::Project *project = ProjectExplorer::SessionManager::projectForFile(qmlFileName);
    ProjectExplorer::Node *node = ProjectExplorer::ProjectTree::nodeForFile(qmlFileName)->parentFolderNode();
    ProjectExplorer::Node *iconQrcFileNode = nullptr;

    while (node && !iconQrcFileNode) {
        qCDebug(documentManagerLog) << "Checking" << node->displayName() << "(" << node << ")";

        if (node->isVirtualFolderType() && node->displayName() == "Resources") {
            ProjectExplorer::FolderNode *virtualFolderNode = node->asFolderNode();
            if (QTC_GUARD(virtualFolderNode)) {
                for (int subFolderIndex = 0; subFolderIndex < virtualFolderNode->folderNodes().size() && !iconQrcFileNode; ++subFolderIndex) {
                    ProjectExplorer::FolderNode *subFolderNode = virtualFolderNode->folderNodes().at(subFolderIndex);

                    qCDebug(documentManagerLog) << "Checking if" << subFolderNode->displayName() << "("
                        << subFolderNode << ") is" << isoIconsQrcFile;

                    if (subFolderNode->isFolderNodeType() && subFolderNode->displayName() == isoIconsQrcFile) {
                        qCDebug(documentManagerLog) << "Found" << isoIconsQrcFile << "in" << virtualFolderNode->filePath();

                        iconQrcFileNode = subFolderNode;
                        *resourceFileProPath = iconQrcFileNode->parentProjectNode()->filePath().toString();
                    }
                }
            }
        }

        if (!iconQrcFileNode) {
            qCDebug(documentManagerLog) << "Didn't find" << isoIconsQrcFile
                << "in" << node->displayName() << "; checking parent";
            node = node->parentFolderNode();
        }
    }

    if (!iconQrcFileNode) {
        // The QRC file that we want doesn't exist or is not listed under RESOURCES in the .pro.
        if (project)
            *resourceFilePath = project->projectDirectory().toString() + "/" + isoIconsQrcFile;

        // We assume that the .pro containing the QML file is an acceptable place to add the .qrc file.
        ProjectExplorer::ProjectNode *projectNode = ProjectExplorer::ProjectTree::nodeForFile(qmlFileName)->parentProjectNode();
        *resourceFileProPath = projectNode->filePath().toString();
    } else {
        // We found the QRC file that we want.
        QString projectDirectory = ProjectExplorer::ProjectTree::projectForNode(iconQrcFileNode)->projectDirectory().toString();
        *resourceFilePath = projectDirectory + "/" + isoIconsQrcFile;
    }

    *iconResourceFileAlreadyExists = iconQrcFileNode != nullptr;
}

bool DocumentManager::isoProFileSupportsAddingExistingFiles(const QString &resourceFileProPath)
{
    ProjectExplorer::Node *node = ProjectExplorer::ProjectTree::nodeForFile(Utils::FilePath::fromString(resourceFileProPath));
    if (!node || !node->parentFolderNode())
        return false;
    ProjectExplorer::ProjectNode *projectNode = node->parentFolderNode()->asProjectNode();
    if (!projectNode)
        return false;
    if (!projectNode->supportsAction(ProjectExplorer::AddExistingFile, projectNode)) {
        qCWarning(documentManagerLog) << "Project" << projectNode->displayName() << "does not support adding existing files";
        return false;
    }

    return true;
}

bool DocumentManager::addResourceFileToIsoProject(const QString &resourceFileProPath, const QString &resourceFilePath)
{
    ProjectExplorer::Node *node = ProjectExplorer::ProjectTree::nodeForFile(Utils::FilePath::fromString(resourceFileProPath));
    if (!node || !node->parentFolderNode())
        return false;
    ProjectExplorer::ProjectNode *projectNode = node->parentFolderNode()->asProjectNode();
    if (!projectNode)
        return false;

    if (!projectNode->addFiles({Utils::FilePath::fromString(resourceFilePath)})) {
        qCWarning(documentManagerLog) << "Failed to add resource file to" << projectNode->displayName();
        return false;
    }
    return true;
}

bool DocumentManager::belongsToQmakeProject()
{
    QTC_ASSERT(QmlDesignerPlugin::instance(), return false);

    if (!QmlDesignerPlugin::instance()->currentDesignDocument())
        return false;

    Utils::FilePath qmlFileName = QmlDesignerPlugin::instance()->currentDesignDocument()->fileName();
    ProjectExplorer::Project *project = ProjectExplorer::SessionManager::projectForFile(qmlFileName);
    if (!project)
        return false;

    ProjectExplorer::Node *rootNode = project->rootProjectNode();
    auto proNode = dynamic_cast<QmakeProjectManager::QmakeProFileNode*>(rootNode);
    return proNode;
}

Utils::FilePath DocumentManager::currentResourcePath()
{
    Utils::FilePath resourcePath = currentProjectDirPath();
    if (resourcePath.isEmpty())
        return currentFilePath().absolutePath();
    return resourcePath;
}

} // namespace QmlDesigner
