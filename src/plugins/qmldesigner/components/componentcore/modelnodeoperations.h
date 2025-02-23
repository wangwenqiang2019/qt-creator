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

#pragma once

#include "selectioncontext.h"

namespace QmlDesigner {

enum class AddFilesResult { Succeeded, Failed, Cancelled };

namespace ModelNodeOperations {

bool goIntoComponent(const ModelNode &modelNode);

void select(const SelectionContext &selectionState);
void deSelect(const SelectionContext &selectionState);
void cut(const SelectionContext &selectionState);
void copy(const SelectionContext &selectionState);
void deleteSelection(const SelectionContext &selectionState);
void toFront(const SelectionContext &selectionState);
void toBack(const SelectionContext &selectionState);
void raise(const SelectionContext &selectionState);
void lower(const SelectionContext &selectionState);
void paste(const SelectionContext &selectionState);
void undo(const SelectionContext &selectionState);
void redo(const SelectionContext &selectionState);
void setVisible(const SelectionContext &selectionState);
void setFillWidth(const SelectionContext &selectionState);
void setFillHeight(const SelectionContext &selectionState);
void resetSize(const SelectionContext &selectionState);
void resetPosition(const SelectionContext &selectionState);
void goIntoComponentOperation(const SelectionContext &selectionState);
void setId(const SelectionContext &selectionState);
void resetZ(const SelectionContext &selectionState);
void reverse(const SelectionContext &selectionState);
void anchorsFill(const SelectionContext &selectionState);
void anchorsReset(const SelectionContext &selectionState);
void layoutRowPositioner(const SelectionContext &selectionState);
void layoutColumnPositioner(const SelectionContext &selectionState);
void layoutGridPositioner(const SelectionContext &selectionState);
void layoutFlowPositioner(const SelectionContext &selectionState);
void layoutRowLayout(const SelectionContext &selectionState);
void layoutColumnLayout(const SelectionContext &selectionState);
void layoutGridLayout(const SelectionContext &selectionState);
void goImplementation(const SelectionContext &selectionState);
void addNewSignalHandler(const SelectionContext &selectionState);
void addSignalHandlerOrGotoImplementation(const SelectionContext &selectionState, bool addAlwaysNewSlot);
void removeLayout(const SelectionContext &selectionContext);
void removePositioner(const SelectionContext &selectionContext);
void moveToComponent(const SelectionContext &selectionContext);
PropertyName getIndexPropertyName(const ModelNode &modelNode);
void addItemToStackedContainer(const SelectionContext &selectionContext);
void increaseIndexOfStackedContainer(const SelectionContext &selectionContext);
void decreaseIndexOfStackedContainer(const SelectionContext &selectionContext);
void addTabBarToStackedContainer(const SelectionContext &selectionContext);
AddFilesResult addImageToProject(const QStringList &fileNames, const QString &directory);
AddFilesResult addFontToProject(const QStringList &fileNames, const QString &directory);
AddFilesResult addSoundToProject(const QStringList &fileNames, const QString &directory);
AddFilesResult addShaderToProject(const QStringList &fileNames, const QString &directory);
void createFlowActionArea(const SelectionContext &selectionContext);
void addTransition(const SelectionContext &selectionState);
void addFlowEffect(const SelectionContext &selectionState, const TypeName &typeName);
void addCustomFlowEffect(const SelectionContext &selectionState);
void setFlowStartItem(const SelectionContext &selectionContext);
void addToGroupItem(const SelectionContext &selectionContext);
void selectFlowEffect(const SelectionContext &selectionContext);
void mergeWithTemplate(const SelectionContext &selectionContext);
void removeGroup(const SelectionContext &selectionContext);
void editAnnotation(const SelectionContext &selectionContext);

void openSignalDialog(const SelectionContext &selectionContext);
void updateImported3DAsset(const SelectionContext &selectionContext);

// ModelNodePreviewImageOperations
QVariant previewImageDataForGenericNode(const ModelNode &modelNode);
QVariant previewImageDataForImageNode(const ModelNode &modelNode);

} // namespace ModelNodeOperationso
} //QmlDesigner
