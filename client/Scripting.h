// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#ifndef SCRIPTING_H_
#define SCRIPTING_H_

#include <memory>

#include <QComboBox>
#include <QPushButton>
#include <QScriptEngine>

class ImageView;

class Scripting {
public:
    Scripting();
    ~Scripting();

    void runScriptThread();
    void handleRunScript();


    std::unique_ptr<QScriptEngine> mScriptEngine;

    void init(ImageView* imageView, const std::string& scriptName, bool exitOnDone);
    void scriptableButton(const std::string& name, std::unique_ptr<QPushButton>& button);
    void scriptableComboBox(const std::string& name, std::unique_ptr<QComboBox>& box);

    std::string mScriptName;
    bool mExitOnDone;
    ImageView* mImageView; // keep a copy of ImageView ptr
};

#endif /* SCRIPTING_H_ */
