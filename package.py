# Copyright 2023-2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import sys

name = 'arras_render'

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '6.30'

    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Simple command line tool to render using Moonray\'s MCRT computation'

authors = ['Dreamworks Animation R&D - JoSE Team & friends', 'psw-jose@dreamworks.com']

help = ('For assistance, '
        "please contact the folio's owner at: psw-jose@dreamworks.com")

variants = [
    [   # variant 0
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2023.1',
        'openimageio-2.4.8.0.x',
        'gcc-11.x'
    ],
    [   # variant 1
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2023.1',
        'openimageio-2.4.8.0.x',
        'clang-17.0.6.x'
    ],
    # Requires that we move to Qt6
    # [
    #     'os-rocky-9',
    #     'opt_level-optdebug',
    #     'refplat-vfx2024.0',
    #     'openimageio-2.4.8.0.x',
    #     'gcc-11.x'
    # ],
    # [
    #     'os-rocky-9',
    #     'opt_level-optdebug',
    #     'refplat-vfx2025.0',
    #     'openimageio-3.0',
    #     'gcc-11.x'
    # ],
    # [
    #     'os-rocky-9',
    #     'opt_level-optdebug',
    #     'refplat-houdini21.0',
    #     'openimageio-3.0',
    #     'gcc-11.x'
    # ],
    [   # variant 2
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2022.0',
        'openimageio-2.3.20.0.x',
        'gcc-9.3.x.1'
    ],
]

requires = [
    # Arras deps
    'arras4_core-4.10',

    # MCRT
    'mcrt_dataio-15.21',
    'moonbase_proxies-14.38',
    'mcrt_messages-14.9',
    'scene_rdl2-15.20',

    # Third party deps
    'boost',
    'jsoncpp-1.9.5',
    'openexr',
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
