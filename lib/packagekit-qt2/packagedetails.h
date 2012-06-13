/***************************************************************************
 *   Copyright (C) 2012 by Daniel Nicoletti                                *
 *   dantti12@gmail.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; see the file COPYING. If not, write to       *
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,  *
 *   Boston, MA 02110-1301, USA.                                           *
 ***************************************************************************/

#ifndef PACKAGEKIT_PACKAGE_DETAILS_H
#define PACKAGEKIT_PACKAGE_DETAILS_H

#include "package.h"

#include <QtCore/QDateTime>

namespace PackageKit {

    class PackageDetailsPrivate : public QSharedData
    {
    public:
        PackageDetailsPrivate() : size(0) {}
        PackageDetailsPrivate(const PackageDetailsPrivate &other) :
        QSharedData(other),
        license(other.license),
        group(other.group),
        detail(other.detail),
        url(other.url),
        size(other.size)
        {}

        QString license;
        uint group;
        QString detail;
        QString url;
        uint size;
    };

/**
 * \class PackageDetails packagedetails.h PackageDetails
 * \author Daniel Nicoletti \e <dantti12@gmail.com>
 *
 * \brief Represents a software package with details
 *
 * This class represents a software package with details.
 */
class PackageDetails : public Package
{
public:
    /**
     * Describes the different package groups
     */
    enum Group {
        GroupUnknown         = 1UL << 0,
        GroupAccessibility   = 1UL << 1,
        GroupAccessories     = 1UL << 2,
        GroupAdminTools      = 1UL << 3,
        GroupCommunication   = 1UL << 4,
        GroupDesktopGnome    = 1UL << 5,
        GroupDesktopKde      = 1UL << 6,
        GroupDesktopOther    = 1UL << 7,
        GroupDesktopXfce     = 1UL << 8,
        GroupEducation       = 1UL << 9,
        GroupFonts           = 1UL << 10,
        GroupGames           = 1UL << 11,
        GroupGraphics        = 1UL << 12,
        GroupInternet        = 1UL << 13,
        GroupLegacy          = 1UL << 14,
        GroupLocalization    = 1UL << 15,
        GroupMaps            = 1UL << 16,
        GroupMultimedia      = 1UL << 17,
        GroupNetwork         = 1UL << 18,
        GroupOffice          = 1UL << 19,
        GroupOther           = 1UL << 20,
        GroupPowerManagement = 1UL << 21,
        GroupProgramming     = 1UL << 22,
        GroupPublishing      = 1UL << 23,
        GroupRepos           = 1UL << 24,
        GroupSecurity        = 1UL << 25,
        GroupServers         = 1UL << 26,
        GroupSystem          = 1UL << 27,
        GroupVirtualization  = 1UL << 28,
        GroupScience         = 1UL << 29,
        GroupDocumentation   = 1UL << 30,
        GroupElectronics     = 1UL << 31,
        GroupCollections     = 1UL << 32,
        GroupVendor          = 1UL << 33,
        GroupNewest          = 1UL << 34
    };
    typedef qulonglong Groups;

    /**
     * Constructs package
     */
    PackageDetails(const QString &package_id, const QString &license, uint group, const QString &detail, const QString &url, qulonglong size);

    /**
     * Constructs a copy of other.
     */
    PackageDetails(const PackageDetails &other);

    /**
     * Constructs an invalid package.
     */
    PackageDetails();

    /**
     * Destructor
     */
    ~PackageDetails();

    /**
     * Returns the package's license
     * \note this will only return a valid value if hasDetails() returns true
     */
    QString license() const;

    /**
     * Returns the package's group (for example Multimedia, Editors...)
     */
    Group group() const;

    /**
     * Returns the package's long description
     */
    QString detail() const;

    /**
     * Returns the software's homepage url
     */
    QString url() const;

    /**
     * Returns the package's size
     */
    qulonglong size() const;

    /**
     * Copy the other package data
     */
    Package& operator=(const Package &package);

private:
    QSharedDataPointer<PackageDetailsPrivate> d;
};

} // End namespace PackageKit

#endif
