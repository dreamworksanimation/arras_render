// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ImageView.h"
#include "NotifiedValue.h"
#include "Scripting.h"

#include <atomic>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <QScriptEngine>

namespace {

//
// Script for setting a line of status overlay
// 
QScriptValue scriptSetStatusOverlay(QScriptContext* context, QScriptEngine* engine)
{
    QScriptValue globalObject = engine->globalObject();
    QObject* object = globalObject.property("imageView").toQObject();
    ImageView* imageView = static_cast<ImageView*>(object);

    if (context->argumentCount() == 2) {
        unsigned short row = context->argument(0).toUInt16();
        std::string message = context->argument(1).toString().toStdString();
        imageView->setStatusOverlay(row, message);
    } else if (context->argumentCount() == 1) {
        unsigned short row = context->argument(0).toUInt16();
        imageView->setStatusOverlay(row, std::string());
    }

    return QScriptValue(0);
}

//
// Script for clearing all of the status overlay
// 
QScriptValue scriptClearStatusOverlay(QScriptContext* context, QScriptEngine* engine)
{
    QScriptValue globalObject = engine->globalObject();
    QObject* object = globalObject.property("imageView").toQObject();
    ImageView* imageView = static_cast<ImageView*>(object);

    if (context->argumentCount() == 0) {
        imageView->setStatusOverlay(-1, "");
    }
    return QScriptValue(0);
}
//
// Script extension for getting the percentage done or waiting for a minumum value
// 
QScriptValue scriptWaitForPercentageDone(QScriptContext* context, QScriptEngine* engine)
{
    QScriptValue globalObject = engine->globalObject();
    QObject* object = globalObject.property("imageView").toQObject();
    ImageView* imageView = static_cast<ImageView*>(object);
    float currentValue = 0.0;

    if (context->argumentCount() == 1) {
        float value = static_cast<float>(context->argument(0).toNumber());
        currentValue = imageView->mRenderProgress.getWhenGreaterOrEqualTo(value);
    } else {
        currentValue = imageView->mRenderProgress.get();
    }
    return QScriptValue(currentValue);
}

//
// Script extension for getting the render instance or waiting for a minumum value
// 
QScriptValue scriptWaitForInstance(QScriptContext* context, QScriptEngine* engine)
{
    QScriptValue globalObject = engine->globalObject();
    QObject* object = globalObject.property("imageView").toQObject();
    ImageView* imageView = static_cast<ImageView*>(object);
    int currentValue = 0;

    if (context->argumentCount() == 1) {
        int value = static_cast<int>(context->argument(0).toNumber());
        currentValue = imageView->mRenderInstance.getWhenGreaterOrEqualTo(value);
    } else {
        currentValue = imageView->mRenderInstance.get();
    }
    return QScriptValue(currentValue);
}

//
/// Script extension for sleeping
//
QScriptValue scriptUSleep(QScriptContext* context, QScriptEngine* engine)
{
    if (context->argumentCount() == 1) {
        int value = static_cast<int>(context->argument(0).toNumber());
        usleep(value);
    }
    return QScriptValue(0);
}

} // end anonymous namespace

std::atomic<bool> scriptRunning(false);

Scripting::Scripting()
    : mExitOnDone(false)
    , mImageView(nullptr)
{
}

Scripting::~Scripting()
{
}

namespace {

// utility function to load the contents of a ascii file into a string
//
std::string
get_file_contents(const std::string& filename)
{
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (in) {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        return (contents);
    }
    throw(errno);
}

} // end anonymous namespace

//
// Thread function for running the script
//
void
Scripting::runScriptThread()
{

    scriptRunning = true;

    if (mScriptName.length() == 0) {
        scriptRunning = false;
        return;
    }

    std::string script = get_file_contents(mScriptName);

    QScriptSyntaxCheckResult result = mScriptEngine->checkSyntax(script.c_str());

    // check for a syntax error
    if (result.state() != QScriptSyntaxCheckResult::Valid) {
        int lineNumber = result.errorLineNumber();
        int columnNumber = result.errorColumnNumber();
        std::string message = result.errorMessage().toStdString();
        std::cout << "Syntax error in the script Line:" << lineNumber 
                  << " Column: " << columnNumber << ": " << message  << "\n";
        scriptRunning = false;
        return;
    }


    mScriptEngine->evaluate(script.c_str()).toNumber();

    // check for a runtime error
    if (mScriptEngine->hasUncaughtException()) {
        int lineNumber = mScriptEngine->uncaughtExceptionLineNumber();
        std::string exception = mScriptEngine->uncaughtException().toString().toStdString();
        std::cout << "Exception in the script Line: " << lineNumber << " : "
                  << exception << "\n";
        
    }
    if (mExitOnDone) {
        mImageView->exitProgram();
    }
    scriptRunning = false;
}

//
// start the script in a separate thread. Normal operation of manual buttons will still be available.
//
void
Scripting::handleRunScript()
{
    if (scriptRunning) return;
    scriptRunning = true;

    std::thread scriptThread(&Scripting::runScriptThread, this);
    scriptThread.detach();
}

// add a button so that it can be manipulated from the scripting environment
void
Scripting::scriptableButton(const std::string& name, std::unique_ptr<QPushButton>& button)
{
    mScriptEngine->globalObject().setProperty(name.c_str(), mScriptEngine->newQObject(button.get()));
}

void
Scripting::scriptableComboBox(const std::string& name, std::unique_ptr<QComboBox>& button)
{
    mScriptEngine->globalObject().setProperty(name.c_str(), mScriptEngine->newQObject(button.get()));
}

void
Scripting::init(ImageView* imageView, const std::string& scriptName, bool exitOnDone)
{
    mImageView = imageView;
    mScriptName = scriptName;
    mExitOnDone = exitOnDone;

    mScriptEngine = std::unique_ptr<QScriptEngine>(new QScriptEngine);

    mScriptEngine->globalObject().setProperty("imageView", mScriptEngine->newQObject(imageView), QFlag(0));

    // add a sleep function to the scripting environment
    mScriptEngine->globalObject().setProperty("usleep", mScriptEngine->newFunction(scriptUSleep, 0));
    mScriptEngine->globalObject().setProperty("waitForPercentageDone", mScriptEngine->newFunction(scriptWaitForPercentageDone, 0));
    mScriptEngine->globalObject().setProperty("waitForInstance", mScriptEngine->newFunction(scriptWaitForInstance, 0));
    mScriptEngine->globalObject().setProperty("setStatusOverlay", mScriptEngine->newFunction(scriptSetStatusOverlay, 0));
    mScriptEngine->globalObject().setProperty("clearStatusOverlay", mScriptEngine->newFunction(scriptClearStatusOverlay, 0));
}
