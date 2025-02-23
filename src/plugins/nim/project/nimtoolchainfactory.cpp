/****************************************************************************
**
** Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
** Contact: http://www.qt.io/licensing
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

#include "nimtoolchainfactory.h"

#include "nimconstants.h"
#include "nimtoolchain.h"

#include <projectexplorer/devicesupport/devicemanager.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/pathchooser.h>
#include <utils/fileutils.h>

#include <QFormLayout>
#include <QProcess>

using namespace ProjectExplorer;
using namespace Utils;

namespace Nim {

NimToolChainFactory::NimToolChainFactory()
{
    setDisplayName(NimToolChain::tr("Nim"));
    setSupportedToolChainType(Constants::C_NIMTOOLCHAIN_TYPEID);
    setSupportedLanguages({Constants::C_NIMLANGUAGE_ID});
    setToolchainConstructor([] { return new NimToolChain; });
    setUserCreatable(true);
}

QList<ToolChain *> NimToolChainFactory::autoDetect(const QList<ToolChain *> &alreadyKnown,
                                                   const IDevice::Ptr &device)
{
    QList<ToolChain *> result;

    IDevice::ConstPtr dev = device ? device : DeviceManager::defaultDesktopDevice();
    const FilePath compilerPath = dev->searchExecutableInPath("nim");
    if (compilerPath.isEmpty())
        return result;

    result = Utils::filtered(alreadyKnown, [compilerPath](ToolChain *tc) {
        return tc->typeId() == Constants::C_NIMTOOLCHAIN_TYPEID
                && tc->compilerCommand() == compilerPath;
    });

    if (!result.empty())
        return result;

    auto tc = new NimToolChain;
    tc->setDetection(ToolChain::AutoDetection);
    tc->setCompilerCommand(compilerPath);
    result.append(tc);
    return result;
}

QList<ToolChain *> NimToolChainFactory::detectForImport(const ToolChainDescription &tcd)
{
    QList<ToolChain *> result;
    if (tcd.language == Constants::C_NIMLANGUAGE_ID) {
        auto tc = new NimToolChain;
        tc->setDetection(ToolChain::ManualDetection); // FIXME: sure?
        tc->setCompilerCommand(tcd.compilerPath);
        result.append(tc);
    }
    return result;
}

NimToolChainConfigWidget::NimToolChainConfigWidget(NimToolChain *tc)
    : ToolChainConfigWidget(tc)
    , m_compilerCommand(new PathChooser)
    , m_compilerVersion(new QLineEdit)
{
    // Create ui
    const auto gnuVersionArgs = QStringList("--version");
    m_compilerCommand->setExpectedKind(PathChooser::ExistingCommand);
    m_compilerCommand->setCommandVersionArguments(gnuVersionArgs);
    m_mainLayout->addRow(tr("&Compiler path:"), m_compilerCommand);
    m_compilerVersion->setReadOnly(true);
    m_mainLayout->addRow(tr("&Compiler version:"), m_compilerVersion);

    // Fill
    fillUI();

    // Connect
    connect(m_compilerCommand, &PathChooser::pathChanged, this, &NimToolChainConfigWidget::onCompilerCommandChanged);
}

void NimToolChainConfigWidget::applyImpl()
{
    auto tc = static_cast<NimToolChain *>(toolChain());
    Q_ASSERT(tc);
    if (tc->isAutoDetected())
        return;
    tc->setCompilerCommand(m_compilerCommand->filePath());
}

void NimToolChainConfigWidget::discardImpl()
{
    fillUI();
}

bool NimToolChainConfigWidget::isDirtyImpl() const
{
    auto tc = static_cast<NimToolChain *>(toolChain());
    Q_ASSERT(tc);
    return tc->compilerCommand() != m_compilerCommand->filePath();
}

void NimToolChainConfigWidget::makeReadOnlyImpl()
{
    m_compilerCommand->setReadOnly(true);
}

void NimToolChainConfigWidget::fillUI()
{
    auto tc = static_cast<NimToolChain *>(toolChain());
    Q_ASSERT(tc);
    m_compilerCommand->setFilePath(tc->compilerCommand());
    m_compilerVersion->setText(tc->compilerVersion());
}

void NimToolChainConfigWidget::onCompilerCommandChanged(const QString &path)
{
    auto tc = static_cast<NimToolChain *>(toolChain());
    Q_ASSERT(tc);
    tc->setCompilerCommand(FilePath::fromString(path));
    fillUI();
}

}
