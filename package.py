# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import sys

name = 'arras_render'

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '6.3.0'

    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Simple command line tool to render using Moonray\'s MCRT computation'

authors = ['Dreamworks Animation R&D - JoSE Team & friends', 'psw-jose@dreamworks.com']

help = ('For assistance, '
        "please contact the folio's owner at: psw-jose@dreamworks.com")

variants = [
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug',    'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug',    'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug',    'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug',    'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2023.1', 'gcc-11.x'],
    ['os-rocky-9', 'opt_level-debug',    'refplat-vfx2023.1', 'gcc-11.x'],
    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2023.1', 'clang-17.0.6.x'],
    ['os-rocky-9', 'opt_level-debug',    'refplat-vfx2023.1', 'clang-17.0.6.x'],
]

requires = [
    # Arras deps
    "arras4_core-4.10",

    # MCRT
    "mcrt_dataio-14.17",
    'moonbase_proxies-13.29',
    "mcrt_messages-13.4",
    "scene_rdl2-14.17",
        
    # Third party deps
    "boost",
    "jsoncpp-1.9.5",
    'openimageio-2.3.20.0.x',
    "openexr"
]


private_build_requires = [
    'cmake_modules-1.0',
    'cppunit',
    'qt'
]

def commands():
    prependenv('PATH', '{root}/bin')
    prependenv('ARRAS_SESSION_PATH', '{root}/sessions')

uuid = '6e9b206e-0676-4029-a119-f66e40f13ace'

config_version = 0
