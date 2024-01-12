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
    _version = '6.2.0'

    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Simple command line tool to render using Moonray\'s MCRT computation'

authors = ['Dreamworks Animation R&D - JoSE Team & friends', 'psw-jose@dreamworks.com']

help = ('For assistance, '
        "please contact the folio's owner at: psw-jose@dreamworks.com")

if 'scons' in sys.argv:
    build_system = 'scons'
    build_system_pbr = 'bart_scons-10'
else:
    build_system = 'cmake'
    build_system_pbr = 'cmake_modules'

variants = [
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2020.3', 'gcc-6.3.x.2'],
    ['os-CentOS-7', 'opt_level-debug',    'refplat-vfx2020.3', 'gcc-6.3.x.2'],
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug',    'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug',    'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug',    'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug',    'refplat-vfx2022.0', 'gcc-9.3.x.1'],
]

scons_targets = ['@install']
sconsTargets = {
    'refplat-vfx2020.3' : scons_targets,
    'refplat-vfx2021.0' : scons_targets,
    'refplat-vfx2022.0' : scons_targets,
}

requires = [
    # Arras deps
    "arras4_core-4.10",
        
    # MCRT
    "mcrt_dataio-13.7",
    'moonbase_proxies-12.9',
    "mcrt_messages-12.1",
    "scene_rdl2-13.4",
        
    # Third party deps
    "boost",
    "jsoncpp-1.9.5",
    'openimageio-2.3.20.0.x',
    "openexr"
]


private_build_requires = [
    build_system_pbr,
    'cppunit',
    'qt'
]

def commands():
    prependenv('PATH', '{root}/bin')
    prependenv('ARRAS_SESSION_PATH', '{root}/sessions')

uuid = '6e9b206e-0676-4029-a119-f66e40f13ace'

config_version = 0
