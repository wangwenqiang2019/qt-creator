/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
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

#include "theme_mac.h"

#include <qglobal.h>
#include <QOperatingSystemVersion>

#include <AppKit/AppKit.h>

#if !QT_MACOS_PLATFORM_SDK_EQUAL_OR_ABOVE(__MAC_10_14)
@interface NSApplication (MojaveForwardDeclarations)
@property (strong) NSAppearance *appearance NS_AVAILABLE_MAC(10_14);
@end
#endif

namespace Utils {
namespace Internal {

void forceMacOSLightAquaApperance()
{
#if __has_builtin(__builtin_available)
    if (__builtin_available(macOS 10.14, *))
#else // Xcode 8
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 10, 14, 0))
#endif
        NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
}

bool currentAppearanceIsDark()
{
#if __has_builtin(__builtin_available)
    if (__builtin_available(macOS 10.14, *)) {
        auto appearance = [NSApp.effectiveAppearance
            bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
        return [appearance isEqualToString:NSAppearanceNameDarkAqua];
    }
#endif
    return false;
}

} // Internal
} // Utils
