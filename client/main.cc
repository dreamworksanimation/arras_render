// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "DebugConsoleSetup.h"

#include <atomic>
#include <chrono>
#include <cmath> // round
#include <cstdlib> // std::getenv
#include <fstream>
#include <functional> // bind
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <unistd.h> // sleep
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>

#include <json/json.h>
#include <json/writer.h>

#include <QApplication>
#include <QObject>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>

#include <scene_rdl2/scene/rdl2/BinaryWriter.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/Utils.h>

#include <mcrt_dataio/client/receiver/ClientReceiverFb.h>

#include <message_api/Message.h>
#include <client/api/AcapAPI.h>
#include <client/api/SessionDefinition.h>

#include <mcrt_messages/GenericMessage.h>
#include <mcrt_messages/JSONMessage.h>
#include <mcrt_messages/ProgressiveFrame.h>
#include <mcrt_messages/OutputRates.h>
#include <mcrt_messages/RDLMessage.h>
#include <mcrt_messages/RenderMessages.h>
#include <mcrt_messages/CreditUpdate.h>
#include <mcrt_messages/ProgressMessage.h>

#include <arras4_log/Logger.h>

#include <sdk/sdk.h>

#include "encodingUtil.h"
#include "ImageView.h"
#include "outputRate.h"

using namespace arras_render;
using namespace std::literals::string_literals;
namespace bpo = boost::program_options;

unsigned short constexpr DEFAULT_CON_WAIT_SECS = 30;
unsigned short constexpr DEFAULT_LOG_LEVEL = 2;
unsigned short constexpr DEFAULT_ACAP_PORT = 8087;
constexpr float ONE_MB_IN_BYTES = 1024.0f * 1024.0f;

const std::string DEFAULT_PROG_SESSION_NAME = "mcrt_progressive"s;
const std::string MULTI_PROG_SESSION_NAME = "mcrt_progressive_n"s;

const std::string DEFAULT_ENV_NAME = "prod"s;
const std::string DEFAULT_ACAP_PATH = "/coordinator/1/sessions"s;

// name of the environment read from ENV_CONTEXTS, if it exists
const std::string ENV_CONTEXT_NAME = "arras_moonray"s;

std::atomic<bool> delayedRender(false);
std::atomic<bool> frameWritten(false);
std::atomic<bool> arrasStopped(false);
std::atomic<bool> arrasExceptionThrown(false);
std::atomic<ImageView*> pImageView(nullptr);

std::atomic<bool> receivedFirstPixels(false);
std::atomic<bool> reachedOnePercent(false);
std::atomic<bool> reachedTenPercent(false);
std::chrono::time_point<std::chrono::steady_clock> renderStart = std::chrono::steady_clock::now();
std::chrono::time_point<std::chrono::steady_clock> beforeCreateSession;
NotifiedValue<float> progressPercent(0.0);
std::atomic<bool> benchmarkMode(false);
std::atomic<bool> showStats(false); // show ClientReceiverFb's statistical info

void
setTelemetryClientMessage(const std::string& msg)
{
    if (pImageView) {
        pImageView.load()->getFbReceiver()->setClientMessage(msg);
        pImageView.load()->displayFrame();
    }
}

const std::string
getStudioName()
{
    char *studio_pc = std::getenv("STUDIO");
    if (studio_pc) {
        std::string studio(studio_pc);
        boost::to_lower(studio);
        return studio;
    } else {
        return "unknown_studio";
    }
}

std::string
getElapsedString(std::chrono::steady_clock::duration elapsedSeconds)
{
    std::chrono::seconds durationSeconds(std::chrono::duration_cast<std::chrono::seconds>(elapsedSeconds));
    unsigned int seconds = static_cast<unsigned int>(durationSeconds.count());
    unsigned int minutes = seconds / 60;
    seconds -= minutes * 60;
    unsigned int hours = minutes / 60;
    minutes  -= hours * 60;


    boost::format timeFmt("%02d:%02d:%02d");
    timeFmt % hours
            % minutes
            % seconds;
    return timeFmt.str();
}


void
parseCmdLine(int argc, char* argv[],
             bpo::options_description& flags, bpo::variables_map& cmdOpts) {
    flags.add_options()
        ("help", "produce help message")
        ("env", bpo::value<std::string>()->default_value("prod"s))
        ("dc", bpo::value<std::string>()->default_value(getStudioName()))
        ("host", bpo::value<std::string>(), "ACAP host name, if unspecified ACAP will be located using the studio's config service")
        ("port", bpo::value<unsigned short>()->default_value(DEFAULT_ACAP_PORT), "ACAP port number, ignored unless --host is specified")
        ("session,s", bpo::value<std::string>(), "Name of Arras session to use")
        ("production,p", bpo::value<std::string>()->default_value(""s), "Production")
        ("sequence", bpo::value<std::string>()->default_value(""s), "Sequence")
        ("shot", bpo::value<std::string>()->default_value(""s), "Shot")
        ("assetGroup", bpo::value<std::string>()->default_value(""s), "Asset Group")
        ("asset", bpo::value<std::string>()->default_value(""s), "Asset")
        ("department", bpo::value<std::string>()->default_value(""s), "Department")
        ("team", bpo::value<std::string>()->default_value(""s), "Team")
        ("log-level,l", bpo::value<unsigned short>()->default_value(DEFAULT_LOG_LEVEL), "Log level [0-5] with 5 being the highest")
        ("gui", bpo::bool_switch()->default_value(true), "Display frames in a gui window")
        ("no-gui", bpo::bool_switch()->default_value(false), "Disable gui")
        ("overlay", bpo::bool_switch()->default_value(false), "Display progress info in an overlay in the gui window")
        ("overlayFont", bpo::value<std::string>()->default_value(ImageViewDefaults::DEFAULT_FONT_NAME), "Font to use when overlay is enabled")
        ("overlaySize", bpo::value<int>()->default_value(ImageViewDefaults::DEFAULT_FONT_SIZE), "Font size to use when overlay is enabled")
        ("telemetry", bpo::bool_switch()->default_value(false), "Display telemetry info in an overlay in the gui window")
        ("telemetryPanel", bpo::value<std::string>()->default_value(""s), "set initial telemetry panel name")
        ("rdl", bpo::value<std::vector<std::string>>()->multitoken(), "Path to RDL input file(s)")
        ("exr", bpo::value<std::string>(), "Path to output EXR file")
        ("rez-context", bpo::bool_switch()->default_value(false), "Client to resolve rez_context and send with session request, supersedes rez-context-file")
        ("rez-context-file", bpo::value<std::string>(), "Value for rez_context_file, supersedes rez-packages.")
        ("rez-prepend", bpo::value<std::string>()->default_value(""s), "Value to set for rez_packages_prepend, useful for running in a testmap.")
        ("rez-packages", bpo::value<std::string>()->default_value(""s), "Set specific rez packages to use for mcrt computations. Otherwise versions will be inferred from current moonbase_proxies.")
        ("num-mcrt", bpo::value<std::string>()->default_value("1"s), "Number of MCRT computations to use (implies -s rdla_array).")
        ("num-cores", bpo::value<unsigned short>(), "Overrides the number of cores requested by the MCRT computation.")
        ("merge-cores", bpo::value<unsigned short>(), "Overrides the number of cores requested by the MERGE computation.")
        ("local-only", bpo::bool_switch(), "Force all computations to run locally.")
        ("no-local", bpo::bool_switch(), "Force all computations to run in the pool.")
        ("fps", bpo::value<unsigned short>(), "Overrides the frame rate for the MCRT computation.")
        ("aov-interval", bpo::value<unsigned>()->default_value(10), "Set the interval rate for sending AOVs, a value of 0 disables this feature.")
        ("delay", bpo::bool_switch(), "Delay the starting of the render, requires gui mode.")
        ("con-timeout,t", bpo::value<unsigned short>()->default_value(DEFAULT_CON_WAIT_SECS), "Amount of time in seconds to wait for client connection.")
        ("script", bpo::value<std::string>()->default_value(""s), "A script to run immediately or when Run Script is selected")
        ("run-script", bpo::bool_switch(), "Run the script immediately")
        ("exit-after-script", bpo::bool_switch(), "Exit after script is done")
        ("auto-credit-off","disable sending out credit after each frame is received")
        ("lag-ms",bpo::value<unsigned>()->default_value(0),"Simulate network delay by sleeping for n milliseconds")
        ("athena-env",bpo::value<std::string>()->default_value("prod"s),"Environment for Athena logging")
        ("trace-level",bpo::value<int>()->default_value(0),"trace threshold level (-1=none,5=max)")
        ("min-update-ms",bpo::value<unsigned>()->default_value(0), "minimum camera update interval (milliseconds)")
        ("benchmark", bpo::bool_switch()->default_value(false), "When used with --no-gui, enable benchmark mode")
        ("progress-channel", bpo::value<std::string>()->default_value(std::string("default"s)), "Channel to send progress/status")
        ("no-scale", bpo::bool_switch(), "Don't scale the image on startup.")
        ("infoRec",bpo::value<float>()->default_value(0.0f),"infoRec interval (sec). disable if set 0.0")
        ("infoRecDisp",bpo::value<float>()->default_value(10.0f),"infoRec display interval (sec)")
        ("infoRecFile",bpo::value<std::string>()->default_value("./run_"s),"set infoRec filename")
        ("showStats",bpo::bool_switch()->default_value(false), "Display clientReceiverFb's statistical info to the cerr")
        ("debug-console",bpo::value<int>()->default_value(-1),"specify debug console port.")
        ("current-env",bpo::bool_switch()->default_value(false), "Use current environment as computation environment")
    ;

    bpo::positional_options_description positionals;
    positionals.add("exr", 1);

    bpo::store(bpo::command_line_parser(argc, argv).
               options(flags).positional(positionals).run(), cmdOpts);
    bpo::notify(cmdOpts);
}


std::string
getArrasUrl(arras4::sdk::SDK& sdk, const bpo::variables_map& cmdOpts)
{
    std::string url;
    if (cmdOpts.count("host")) {
        std::ostringstream ss;
        ss << "http://" << cmdOpts["host"].as<std::string>()
           << ":" << cmdOpts["port"].as<unsigned short>()
           << DEFAULT_ACAP_PATH;

        url = ss.str();
    } else {
        url = sdk.requestArrasUrl(cmdOpts["dc"].as<std::string>(), cmdOpts["env"].as<std::string>());
        std::ostringstream msg;
        msg << "Received " << url << " from Studio Config Service."s;
        ARRAS_LOG_DEBUG(msg.str());
    }

    return url;
}

void
parseNumMCRT(const bpo::variables_map& cmdOpts, unsigned short& numMcrtMin, unsigned short& numMcrtMax)
{
    const std::string& numMcrt = cmdOpts["num-mcrt"].as<std::string>();
    std::list<std::string> tmpList;
    boost::split(tmpList, numMcrt, boost::is_any_of("-"));
    if (tmpList.size() == 1) {
        // only specified 1 number
        numMcrtMin = boost::lexical_cast<unsigned short>(tmpList.front().c_str());
        numMcrtMax = numMcrtMin;
    } else if (tmpList.size() == 2) {
        // specified a range: "n-m"
        unsigned short
            tmp0 = boost::lexical_cast<unsigned short>(tmpList.front().c_str()),
            tmp1 = boost::lexical_cast<unsigned short>(tmpList.back().c_str());
        numMcrtMin = std::min(tmp0, tmp1);
        numMcrtMax = std::max(tmp0, tmp1);
    }
}

arras4::client::SessionDefinition
getSessionDefinition(const std::string& sessionName,
                     unsigned short numMcrtMin,
                     unsigned short numMcrtMax,
                     const bpo::variables_map& cmdOpts)
{
    arras4::client::SessionDefinition def = arras4::client::SessionDefinition::load(sessionName);

    //  trace
    int traceLevel = cmdOpts["trace-level"].as<int>();
    def["mcrt"]["traceThreshold"] = traceLevel;
    if (def.has("dispatch")) 
        def["dispatch"]["traceThreshold"] = traceLevel;

    if (def.has("merge")) 
        def["merge"]["traceThreshold"] = traceLevel;

    //  local/no-local
    //     Allowing both local-only and no-local flags to co-exist to allow for
    //     coordinator testing
    if (cmdOpts["local-only"].as<bool>()) {
        def["mcrt"]["requirements"]["local_only"] = "yes"s;
        if (def.has("dispatch"))  
            def["dispatch"]["requirements"]["local_only"] = "yes"s;

        if (def.has("merge")) 
            def["merge"]["requirements"]["local_only"] = "yes"s;
    }

    if (cmdOpts["no-local"].as<bool>()) {
        def["mcrt"]["requirements"]["no_local"] = "yes"s;
    
        if (def.has("dispatch")) 
            def["dispatch"]["requirements"]["no_local"] = "yes"s;

        if (def.has("merge")) 
            def["merge"]["requirements"]["no_local"] = "yes"s;
    }

    //  number of computations
    if (numMcrtMax > 1) {
        if (numMcrtMax == numMcrtMin) {
            def["mcrt"]["arrayExpand"] = numMcrtMin;
        } else {
            def["mcrt"]["arrayMin"] = numMcrtMin;
            def["mcrt"]["arrayMax"] = numMcrtMax;
        }
    }

    // number of cores
    if (cmdOpts.count("num-cores") > 0) {
        Json::Value mcrtResources;
        mcrtResources["cores"] = cmdOpts["num-cores"].as<unsigned short>();

        // send empty strings for min/maxCores to unset the previous values
        mcrtResources["maxCores"] = ""s;
        mcrtResources["minCores"] = ""s;
        def["mcrt"]["requirements"]["resources"] = mcrtResources;
    }
    if (cmdOpts.count("merge-cores") > 0 && def.has("merge")) {
        def["merge"]["requirements"]["resources"]["cores"] = cmdOpts["merge-cores"].as<unsigned short>();
    }

    // frame rate
    if (cmdOpts.count("fps") > 0) {
        unsigned short fps = cmdOpts["fps"].as<unsigned short>();
        def["mcrt"]["fps"] = fps;
        if (def.has("dispatch")) def["dispatch"]["fps"] = fps;
        if (def.has("merge")) def["merge"]["fps"] = fps;
    }

    // rez context
    //   try to attach a context defined in our environment
    bool attached = false;
    try {
        attached = def.attachContext(ENV_CONTEXT_NAME);
    } catch (arras4::client::DefinitionAttachError& e) {
        ARRAS_LOG_WARN("Failed to attach context %s : %s",
                       ENV_CONTEXT_NAME.c_str(), e.what());
    }
    if (attached) {
        ARRAS_LOG_DEBUG("Using computation context from ENV_CONTEXTS");
    } else {
        // no context in environment, so we have to build one based on our own
        // rez context or on arguments provided to the program

        arras4::api::Object envCtx;
        if (cmdOpts.count("rez-context-file") > 0) {
            // supplied as a file
            std::string content;
            std::string rezContextFile(cmdOpts["rez-context-file"].as<std::string>());
            ARRAS_LOG_DEBUG("Reading computation context from "+rezContextFile);
            std::ifstream ctxfile(rezContextFile);
            if (!ctxfile) {
                std::ostringstream ostr;
                ostr << "Could not open rezContextFile:" << rezContextFile;
                throw std::runtime_error(ostr.str());
            }

            ctxfile.seekg(0, std::ios::end);
            content.reserve(ctxfile.tellg());
            ctxfile.seekg(0, std::ios::beg);
            content.assign((std::istreambuf_iterator<char>(ctxfile)),
                            std::istreambuf_iterator<char>());
            envCtx["packaging_system"] = "bash"s;
            envCtx["script"] = content;
        } else if (cmdOpts["current-env"].as<bool>()) {
            // use the current environment of the launching process
            envCtx["packaging_system"] = "current-environment";
        } else {
            // build a new rez environment from our current rez environment
            ARRAS_LOG_DEBUG("Building computation context from rez environment");
            const std::string packagingSystem("rez2");
            const std::string rezPrepend(cmdOpts["rez-prepend"].as<std::string>());
            std::string rezPkgs(cmdOpts["rez-packages"].as<std::string>());
            if (rezPkgs.empty()) {
                // assume we need same moonshine version as the moonbase_proxies
                // we're using
                std::ostringstream ssRezPkgs;
                ssRezPkgs << "mcrt_computation arras4_core moonshine_dwa "
                          << "moonshine-"
                          << std::getenv("REZ_MOONBASE_PROXIES_VERSION");

                rezPkgs = ssRezPkgs.str();
            }

            envCtx["packaging_system"] = packagingSystem;
            envCtx["rez_packages"] = rezPkgs;

            if (!rezPrepend.empty()) 
                envCtx["rez_packages_prepend"] = rezPrepend;
        }

        def.attachContextObject(ENV_CONTEXT_NAME,envCtx);
    }

    return def;
}

bool
connect(arras4::sdk::SDK& sdk,
        const std::string& sessionName,
        unsigned short numMcrtMin,
        unsigned short numMcrtMax,
        const bpo::variables_map& cmdOpts)
{
    try {
        arras4::client::SessionOptions so;
        so.setProduction(cmdOpts["production"].as<std::string>()).  \
            setSequence(cmdOpts["sequence"].as<std::string>()).     \
            setShot(cmdOpts["shot"].as<std::string>()).             \
            setAssetGroup(cmdOpts["assetGroup"].as<std::string>()). \
            setAsset(cmdOpts["asset"].as<std::string>()).           \
            setDepartment(cmdOpts["department"].as<std::string>()). \
            setTeam(cmdOpts["team"].as<std::string>());

        auto def = getSessionDefinition(sessionName, numMcrtMin, numMcrtMax, cmdOpts);
        std::string errString;
        auto beforeResolve = std::chrono::steady_clock::now();
        beforeCreateSession = std::chrono::steady_clock::now();

        std::string resolveTime;
        if (cmdOpts["rez-context"].as<bool>()) {
            bool hasClientReq = def["(client)"].isMember("requirements");
            ARRAS_LOG_INFO("Resolving context...");
            if (!sdk.resolveRez(def, errString)) {
                ARRAS_LOG_ERROR("Couldn't resolve context. Got error %s", errString.c_str());
            }

            beforeCreateSession = std::chrono::steady_clock::now();
            resolveTime = getElapsedString(std::chrono::duration_cast<std::chrono::seconds>(beforeCreateSession - beforeResolve));
            ARRAS_LOG_INFO("Time to resolve rez context ", sdk.sessionId().c_str(), resolveTime.c_str());
            if (benchmarkMode) {
                std::cout << "BENCHMARK Time to resolve rez context " << resolveTime << std::endl;
            }
            // work around ARRAS-3647
            if (!hasClientReq && def["(client)"].isMember("requirements")) {
                def["(client)"].removeMember("requirements");
            }
        }

        const std::string& arrasUrl = getArrasUrl(sdk, cmdOpts);
        ARRAS_LOG_INFO("Finished getting service url. Creating session");
        const std::string& response = sdk.createSession(def, arrasUrl, so);
        if (response.empty()) {
            ARRAS_LOG_ERROR("Failed to connect to Arras service: %s", arrasUrl.c_str());
            return false;
        }

        const auto& afterCreateSession = std::chrono::steady_clock::now();
        resolveTime = getElapsedString(std::chrono::duration_cast<std::chrono::seconds>(afterCreateSession - beforeCreateSession));
        ARRAS_LOG_INFO("Time to create session (session %s) %s", sdk.sessionId().c_str(), resolveTime.c_str());
        if (benchmarkMode) { 
            std::cout << "BENCHMARK Time to create (session " << sdk.sessionId() << " ) " <<
                         resolveTime << std::endl;
        }

        std::cout << "Created session id " << response << std::endl;

    } catch (const arras4::sdk::SDKException& e) {
        ARRAS_LOG_ERROR("Unable to connect to Arras: %s", e.what());
        return false;
    } catch (const arras4::client::DefinitionLoadError& e) {
        ARRAS_LOG_ERROR("Failed to load session: %s", e.what());
        return false;
    } catch (const std::runtime_error& e) {
        ARRAS_LOG_ERROR("Failed getSessionDefinition: %s", e.what());
        return false;
    }

    return true;
}


bool
isFinal(const mcrt::ProgressiveFrame& frame)
{
    return frame.getStatus() == mcrt::ProgressiveFrame::FINISHED;
}

void
printFrameStats(std::shared_ptr<arras4::sdk::SDK> pSdk, const mcrt::ProgressiveFrame& frame)
{
    const auto statusId = frame.getStatus();
    float progress = frame.getProgress() * 100.0f;
    unsigned short roundedProgress = static_cast<unsigned short>(round(progress));
    progressPercent = progress;

    std::string status;
    bool finalFrame = false;
    switch (statusId) {
        case mcrt::ProgressiveFrame::STARTED:
            status = "started"s;
            pSdk->progress("Render started"s);
            break;
        case mcrt::ProgressiveFrame::RENDERING:
            status = "rendering"s;
            pSdk->progress("Rendering", roundedProgress);
            break;
        case mcrt::ProgressiveFrame::FINISHED:
            status = "finished"s;
            pSdk->progress("Render finished"s);
            finalFrame = true;
            break;
        case mcrt::ProgressiveFrame::CANCELLED:
            status = "canceled"s;
            pSdk->progress("Render canceled"s);
            break;
        default:
            status = "error"s;
            pSdk->progress("Render error"s);
            break;
    }

    uint32_t frameSize = 0;
    for (const auto& b : frame.mBuffers) {
        frameSize += b.mDataLength;
    }

    float frameSizeMB = static_cast<float>(frameSize) / ONE_MB_IN_BYTES;
    auto now = std::chrono::steady_clock::now();
    std::string elapsedTime = getElapsedString(std::chrono::duration_cast<std::chrono::seconds>(now - renderStart));
    std::ostringstream msg;
    msg << "sessionid " << pSdk->sessionId()
        << " Time " << elapsedTime 
        << " Received Frame ("
        << frame.getWidth() << "x" << frame.getHeight() << ")"
        << "\tStatus: " << status
        << "\tBuffers: " << frame.mBuffers.size()
        << "\tSize: " << frameSizeMB << "MB"
        << "\tProgress: " << roundedProgress << "%"
        << "\tFinal: " << finalFrame
        << "\tFirst: " << !receivedFirstPixels;

    ARRAS_LOG_INFO(msg.str());
    if (benchmarkMode) {
        std::cout << "Progress " << progress << " ( " <<
                      std::setw(2) <<
                      frameSizeMB << " MB)" <<
                      std::endl;
    }

    receivedFirstPixels = true;
}

void
messageHandler(std::shared_ptr<arras4::sdk::SDK> pSdk,
               bool autoCredit,
               unsigned lag,
               std::shared_ptr<mcrt_dataio::ClientReceiverFb> pFbReceiver,
               const std::string& exrFileName,
               const arras4::api::Message& msg)
{
    pFbReceiver->updateStatsMsgInterval(); // update message interval statistical info

    if (msg.classId() == mcrt::GenericMessage::ID) {
        mcrt::GenericMessage::ConstPtr gm = msg.contentAs<mcrt::GenericMessage>();
        ARRAS_LOG_DEBUG("Received GenericMessage: %s", gm->mValue.c_str());

    } else if (msg.classId() == mcrt::JSONMessage::ID) {
        // Why yes I did borrow this code from Moonray4Maya, thanks Alan
        mcrt::JSONMessage::ConstPtr jm = msg.contentAs<mcrt::JSONMessage>();
        const std::string messageID = jm->messageId();
        if (messageID == mcrt::RenderMessages::PICK_DATA_MESSAGE_ID) {
            Json::StyledWriter jw;
            std::ostringstream ostr;
            ostr << "PICK_DATA_MESSAGE " << jw.write(jm->messagePayload());
            std::cerr << ostr.str();
            pFbReceiver->consoleDriver().showString(ostr.str() + '\n');
            
        } else {
            auto& payload = jm->messagePayload();
            auto& logMsg = payload[mcrt::RenderMessages::LOGGING_MESSAGE_PAYLOAD_STRING];
            ARRAS_LOG_DEBUG("[Moonray]: %s", logMsg.asCString());
        }

    } else if (msg.classId() == mcrt::ProgressiveFrame::ID) {

        if (lag > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(lag));
        }

        if (autoCredit) {
            mcrt::CreditUpdate::Ptr creditMsg = std::make_shared<mcrt::CreditUpdate>();
            creditMsg->value() = 1;
            pSdk->sendMessage(creditMsg);
        }

        mcrt::ProgressiveFrame::ConstPtr frameMsg =  msg.contentAs<mcrt::ProgressiveFrame>();

        printFrameStats(pSdk, *frameMsg);

        size_t totalSize = sizeof(mcrt::ProgressiveFrame);
        {
            if (pImageView) {
                pImageView.load()->getFrameMux().lock();
            }
            pFbReceiver->decodeProgressiveFrame(*frameMsg, true,
                                                [&]() {} /*no-op callback for started condition */,
                                                [&](const std::string &comment) { // genericComment callBack func
                                                    std::cerr << ">> main.cc " << comment << '\n';
                                                });
            if (pImageView) {
                pImageView.load()->getFrameMux().unlock();
            }
        }
        for (size_t i=0; i < frameMsg->mBuffers.size(); i++) {
            totalSize += sizeof(mcrt::BaseFrame::DataBuffer);
            totalSize += frameMsg->mBuffers[i].mDataLength;
        }

        if (pFbReceiver->getProgress() >= 0.0f) {
            // If getProgress() returns a negative value, image data is not received yet.
            if (pImageView != nullptr) {
                pImageView.load()->displayFrame();
            } else {
                std::cerr << ">> main.cc pImageView is nullptr!!!\n";
            }

            if (isFinal(*frameMsg) && !exrFileName.empty()) {
                writeExrFile(exrFileName, *pFbReceiver);
                frameWritten = true;
            }

            pFbReceiver->updateStatsProgressiveFrame(); // update progressiveFrame message info

            if (showStats) {
                // statistical info shows every 3 sec
                std::string msg;
                if (pFbReceiver->getStats(3.0f, msg)) {
                    std::cerr << msg << " recvImgFps:" << pFbReceiver->getRecvImageDataFps() << '\n';
                }
            }
        }
    } else if (msg.classId() == mcrt::ProgressMessage::ID) {
        // ignore this message
    } else {
        ARRAS_LOG_DEBUG("Ignoring unrecognized message %s", msg.describe().c_str());
    }
}

std::unique_ptr<scene_rdl2::rdl2::SceneContext>
sceneFromRDLFiles(const std::vector<std::string>& rdlFiles) {
    auto sc = std::make_unique<scene_rdl2::rdl2::SceneContext>();
    sc->setProxyModeEnabled(true);

    ARRAS_LOG_DEBUG("RDL files:");
    for (const auto& rdlFile: rdlFiles) {
        ARRAS_LOG_DEBUG("\t%s", rdlFile.c_str());
        scene_rdl2::rdl2::readSceneFromFile(rdlFile, *sc);
    }

    sc->commitAllChanges();
    return sc;
}

void
sendRDL(arras4::sdk::SDK& sdk, scene_rdl2::rdl2::SceneContext& sc)
{
    receivedFirstPixels = false;
    ARRAS_LOG_DEBUG("Creating RDL Message");
    mcrt::RDLMessage::Ptr rdlMsg = std::make_shared<mcrt::RDLMessage>();

    scene_rdl2::rdl2::BinaryWriter w(sc);
    w.toBytes(rdlMsg->mManifest, rdlMsg->mPayload);
    
    rdlMsg->mSyncId = 0; // initial syncId

    ARRAS_LOG_DEBUG("Sending RDLMessage");
    sdk.sendMessage(rdlMsg);

    if (delayedRender) {
        sdk.sendMessage(mcrt::RenderMessages::createControlMessage(true));
    }
}

void
statusHandler(std::shared_ptr<arras4::sdk::SDK> pSdk,
              const std::string& status)
{
    // Check to see if the new status is a json doc
    Json::Value root;
    Json::Reader reader;
    if (reader.parse(status, root)) {
        Json::Value execStatus = root.get("execStatus", Json::Value());
        if (execStatus.isString() && 
            (execStatus.asString() == "stopped"s || 
             execStatus.asString() == "stopping"s)) {
            pSdk->progress("Error"s, "failed"s);

            arrasStopped = true;
            Json::Value stopReason = root.get("execStoppedReason", Json::Value());

            std::ostringstream msg;
            msg << "Arras session has stopped"s;
            if (stopReason.isString()) {
                msg << " due to: " << stopReason.asString();
                pSdk->progressInfo("errors"s, stopReason.asString());
            }

            ARRAS_LOG_WARN(msg.str());
            ARRAS_LOG_WARN("Computation Status:");

            Json::Value computations = root.get("computations", Json::Value());
            if (computations.isArray()) {
                for (const auto& comp : computations) {
                    Json::Value compName = comp.get("name", Json::Value());
                    Json::Value compStopReason = comp.get("stoppedReason", Json::Value());
                    Json::Value compSignal = comp.get("signal", Json::Value());

                    if (compName.isString() && compStopReason.isString()) {
                        msg.str(""s);
                        msg << "\t" << compName.asString() << " stopped due to: " << compStopReason.asString();

                        // when the computation is stopped by a signal (terminate or kill)
                        // then compStopReason == "signal"
                        if (compSignal.isString() && compSignal.asString() != "not set"s) {
                            msg << " " << compSignal.asString();
                        }

                        ARRAS_LOG_WARN(msg.str());
                    }

                }
            }
        }
    } else {
        ARRAS_LOG_INFO("Received status change to: %s", status.c_str());
    }
}

void
exceptionCallback(const std::exception& e)
{
    ARRAS_LOG_ERROR("Thrown exception: %s", e.what());
    arrasExceptionThrown = true;
}

bool
createNewSession(arras4::sdk::SDK& sdk,
                 scene_rdl2::rdl2::SceneContext& sceneCtx,
                 const std::string& sessionName,
                 const unsigned short numMcrtMin,
                 const unsigned short numMcrtMax,
                 const unsigned aovInterval,
                 const bpo::variables_map& cmdOpts,
                 /*out*/int& exitStatus)
{
    std::chrono::time_point<std::chrono::steady_clock> sessionCreateStart = std::chrono::steady_clock::now();
    if (!connect(sdk, sessionName, numMcrtMin, numMcrtMax, cmdOpts)) {
        std::cerr << "Failed to connect!" << std::endl;
        exitStatus = 1;
        return false;
    }

    ARRAS_LOG_INFO("Waiting for engine ready");
    setTelemetryClientMessage("Waiting for engine ready");
    bool ready = sdk.waitForEngineReady(cmdOpts["con-timeout"].as<unsigned short>());

    if (!sdk.isConnected() || !ready || arrasStopped) {
        std::cerr << "Failed to connect!" << std::endl;
        exitStatus = 1;
        return false;
    }

    {
        std::chrono::time_point<std::chrono::steady_clock> sessionCreateDone = std::chrono::steady_clock::now();
        std::string elapsedString = getElapsedString(sessionCreateDone - beforeCreateSession);
        ARRAS_LOG_INFO("Session create time (session %s) %s", sdk.sessionId().c_str(), elapsedString.c_str());
        if (benchmarkMode) {
            std::cout << "BENCHMARK Session startup time (session " 
                      << sdk.sessionId() << " ) " << elapsedString << std::endl;
        }

        elapsedString = getElapsedString(sessionCreateDone - sessionCreateStart);
        ARRAS_LOG_INFO("Total session startup time (session %s) %s", sdk.sessionId().c_str(), elapsedString.c_str());
        if (benchmarkMode) {
            std::cout << "BENCHMARK Total session startup time (session " 
                      << sdk.sessionId() << " ) " << elapsedString.c_str() << std::endl;
        }
    }

    bool rdlSent = false;
    ARRAS_LOG_INFO("Client connected");
    setTelemetryClientMessage("Client connected");
    while(!rdlSent && sdk.isConnected() && !arrasExceptionThrown && !arrasStopped) {
        if (sdk.isEngineReady()) {
            if (aovInterval > 0) {
                setOutputRate(sdk, aovInterval);
            }

            renderStart = std::chrono::steady_clock::now();
            sendRDL(sdk, sceneCtx);
            rdlSent = true;
            setTelemetryClientMessage("sent RDL");
        }

        sleep(1);
        if (delayedRender && rdlSent) {
            sdk.sendMessage(mcrt::RenderMessages::createControlMessage(true));
        }
    }

    return (sdk.isConnected() && !arrasExceptionThrown && !arrasStopped);
}

void
logBenchmarkStatus(arras4::sdk::SDK& sdk,
                   const char* infoMsg,
                   const std::string& stdoutMsg)
{
    auto now = std::chrono::steady_clock::now();
    std::string elapsedString = getElapsedString(std::chrono::duration_cast<std::chrono::seconds>(now - renderStart));
    ARRAS_LOG_INFO(infoMsg, sdk.sessionId().c_str(), elapsedString.c_str());
    std::cout << stdoutMsg << sdk.sessionId() << " ) " << elapsedString << std::endl;
}

void
benchLoop(arras4::sdk::SDK& sdk)
{
    bool first = false;
    reachedOnePercent=false;
    reachedTenPercent=false;

    while(!frameWritten && sdk.isConnected() && !arrasExceptionThrown && !arrasStopped && (progressPercent < 100.0)) {
        if (!first && receivedFirstPixels) {
            first = true;
            logBenchmarkStatus(sdk, 
                               "Time to first frame on initial render (session %s) %s", 
                               "BENCHMARK Time to first frame on initial render (session"s);
        }

        if (!reachedOnePercent && progressPercent >= 1.0) {
            reachedOnePercent = true;
            logBenchmarkStatus(sdk, 
                               "Time to 1%% on initial render (session %s) %s", 
                               "BENCHMARK Time to 1% on initial render (session "s);
        }

        if (!reachedTenPercent && progressPercent >= 10.0) {
            reachedTenPercent = true;
            logBenchmarkStatus(sdk, 
                               "Time to 10%% on initial render (session %s) %s",
                               "BENCHMARK Time to 10% on initial render (session "s);
        }

        sleep(1);
    }
}

void
execBenchmark(std::shared_ptr<arras4::sdk::SDK> pSdk, 
              std::unique_ptr<scene_rdl2::rdl2::SceneContext> sceneCtx)
{
    // not in gui mode just sleep the main thread until we are done
    // or something bad happened

    benchLoop(*pSdk);
    logBenchmarkStatus(*pSdk, 
                       "Time to 100%% on initial render (session %s) %s",
                       "BENCHMARK Time to 100% on initial render (session "s);

    scene_rdl2::rdl2::BinaryWriter w(*sceneCtx);
    w.setDeltaEncoding(true);

    mcrt::RDLMessage::Ptr rdlMsg = std::make_shared<mcrt::RDLMessage>();
    w.toBytes(rdlMsg->mManifest, rdlMsg->mPayload);
    rdlMsg->mForceReload = false;

    rdlMsg->mSyncId = 1;

    sceneCtx->commitAllChanges();
    pSdk->sendMessage(rdlMsg);

    renderStart = std::chrono::steady_clock::now();

    // There may still be progress messages from first pass
    // Wait for the second pass to start
    while(!frameWritten && pSdk->isConnected() && !arrasExceptionThrown && !arrasStopped) {
        sleep(1);
    }

    benchLoop(*pSdk);
    logBenchmarkStatus(*pSdk, 
                       "Time to 100%% on second render (session %s) %s",
                       "BENCHMARK Time to 100% on second render (session "s);
}

int
main(int argc, char* argv[])
{
    // make cout unbuffered
    std::cout.setf(std::ios::unitbuf);
    std::chrono::time_point<std::chrono::steady_clock> programStart = std::chrono::steady_clock::now();

    bpo::options_description flags;
    bpo::variables_map cmdOpts;

    try {
        parseCmdLine(argc, argv, flags, cmdOpts);
    } catch(std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    } catch(...) {
        std::cerr << "Exception of unknown type!" << std::endl;;
        return 1;
    }

    if (cmdOpts.count("help")) {
        std::cout << flags << std::endl;
        return 0;
    }

    bool autoCredit = cmdOpts.count("auto-credit-off") == 0;
    unsigned lag = cmdOpts["lag-ms"].as<unsigned>();

    std::chrono::milliseconds minUpdateMs(cmdOpts["min-update-ms"].as<unsigned>());
    std::chrono::steady_clock::duration minUpdateInterval = 
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(minUpdateMs);

    arras4::sdk::SDK::configAthenaLogger(cmdOpts["athena-env"].as<std::string>());

    arras4::log::Logger::Level logLevel;
    auto ll = cmdOpts["log-level"].as<unsigned short>();
    if (ll <= arras4::log::Logger::LOG_TRACE) {
        logLevel = static_cast<arras4::log::Logger::Level>(ll);
    } else {
        std::cerr << "Supported log levels are 0-5" << std::endl;
        return 1;
    }
    arras4::log::Logger::instance().setThreshold(logLevel);
    arras4::log::Logger::Level traceLevel = static_cast<arras4::log::Logger::Level>(cmdOpts["trace-level"].as<int>());
    arras4::log::Logger::instance().setTraceThreshold(traceLevel);

    delayedRender = cmdOpts["delay"].as<bool>();
    bool guiMode = cmdOpts["gui"].as<bool>() && !cmdOpts["no-gui"].as<bool>();

    benchmarkMode = cmdOpts["benchmark"].as<bool>();
    showStats = cmdOpts["showStats"].as<bool>();

    if (delayedRender && ! guiMode) {
        std::cerr << "--delay requires --gui" << std::endl;
        return 1;
    }

    std::vector<std::string> rdlFiles;
    std::string exrFile;
    if (cmdOpts.count("rdl")) {
        rdlFiles = cmdOpts["rdl"].as<std::vector<std::string>>();
    } else {
        std::cerr << "At least one RDL file is required" << std::endl;
        std::cerr << flags << std::endl;
        return 1;
    }

    if (cmdOpts.count("exr")) {
        exrFile = cmdOpts["exr"].as<std::string>();
    } else if (!guiMode){
        std::cerr << "Either --gui or a path to an exr output file is required" << std::endl;
        std::cerr << flags << std::endl;
        return 1;
    }

    std::unique_ptr<scene_rdl2::rdl2::SceneContext> pSceneCtx(sceneFromRDLFiles(rdlFiles));
    bool initialTelemetryOverlayCondition = cmdOpts["telemetry"].as<bool>();
    std::shared_ptr<mcrt_dataio::ClientReceiverFb> pFbReceiver =
        std::make_shared<mcrt_dataio::ClientReceiverFb>(initialTelemetryOverlayCondition);
    std::shared_ptr<arras4::sdk::SDK> pSdk = std::make_shared<arras4::sdk::SDK>();

    pSdk->setAsyncSend(); // async send mode

    pFbReceiver->setInfoRecInterval(cmdOpts["infoRec"].as<float>());
    pFbReceiver->setInfoRecDisplayInterval(cmdOpts["infoRecDisp"].as<float>());
    pFbReceiver->setInfoRecFileName(cmdOpts["infoRecFile"].as<std::string>());
    pFbReceiver->setTelemetryInitialPanel(cmdOpts["telemetryPanel"].as<std::string>());

    pSdk->setMessageHandler(std::bind(&messageHandler,
                                      pSdk,
                                      autoCredit,
                                      lag,
                                      pFbReceiver,
                                      exrFile,
                                      std::placeholders::_1));

    pSdk->setStatusHandler(std::bind(&statusHandler,
                                     pSdk,
                                     std::placeholders::_1));
    pSdk->setExceptionCallback(&exceptionCallback);
    pSdk->setProgressChannel(cmdOpts["progress-channel"].as<std::string>());  

    std::string sessionName;
    unsigned short numMcrtMin = 1, numMcrtMax = 1;
    parseNumMCRT(cmdOpts, numMcrtMin, numMcrtMax);

    if (cmdOpts.count("session")) {
        sessionName = cmdOpts["session"].as<std::string>();
    } else if (numMcrtMax > 1) {
        sessionName = MULTI_PROG_SESSION_NAME;
    } else {
        sessionName = DEFAULT_PROG_SESSION_NAME;
    }
    unsigned aovInterval = cmdOpts["aov-interval"].as<unsigned>();

    std::chrono::time_point<std::chrono::steady_clock> sessionCreateStart = std::chrono::steady_clock::now();

    if (benchmarkMode) {
        std::cout << "Program startup overhead: " 
                  << getElapsedString(sessionCreateStart - programStart) 
                  << std::endl;
    }

    int exitStatus = 0;

    pSdk->progress("Session created"s);
    if (guiMode) {
        // We want to construct ImageView before createnewSession when it is guiMode.
        QApplication app(argc, argv);
        ImageView* imageView = new ImageView(pFbReceiver,
                                             std::move(pSceneCtx),
                                             cmdOpts["overlay"].as<bool>(),
                                             cmdOpts["overlayFont"].as<std::string>(),
                                             cmdOpts["overlaySize"].as<int>(),
                                             sessionName,
                                             numMcrtMin,
                                             numMcrtMax,
                                             aovInterval,
                                             cmdOpts["script"].as<std::string>(),
                                             cmdOpts["exit-after-script"].as<bool>(),
                                             minUpdateInterval,
                                             cmdOpts["no-scale"].as<bool>());
        pImageView.store(imageView);

        setTelemetryClientMessage("imageView construction done");

        auto qtExec = [&]() {
            pImageView.load()->show();
            exitStatus = app.exec();
        };
            
        auto setupSession = [&]() {
            if (!createNewSession(*pSdk,
                                  pImageView.load()->getSceneContext2(),
                                  sessionName,
                                  numMcrtMin,
                                  numMcrtMax,
                                  aovInterval,
                                  cmdOpts,
                                  exitStatus)) {
                std::cerr << ">> main.cc ERROR : createNewSession() failed\n";
                return;
            }
            pImageView.load()->setup(pSdk);

            if (cmdOpts.count("debug-console")) {
                int port = cmdOpts["debug-console"].as<int>();
                if (port > 0) {
                    arras_render::debugConsoleSetup(port, pSdk, pFbReceiver, pImageView);
                }
            }

            if (cmdOpts["run-script"].as<bool>()) {
                pImageView.load()->handleRunScript();
            }
        };

        try {
            // We run the session setup function as an independent thread
            // in order to display Qt window as soon as possible.
            std::thread th2(setupSession);
            qtExec();
            th2.join();
        }
        catch (std::exception &e) {
            std::cerr << e.what() << '\n';
        }

        // close down the connection before ImageView gets destroyed. Otherwise
        // the message handler thread might be using ImageView when it is destroyed
        if (pSdk->isConnected()) {
            if (!arrasExceptionThrown) {
                pSdk->sendMessage(mcrt::RenderMessages::createControlMessage(true));
            }

            pSdk->disconnect();
        }
    } else if (benchmarkMode) {
        if (!createNewSession(*pSdk,
                              *pSceneCtx,
                              sessionName,
                              numMcrtMin,
                              numMcrtMax,
                              aovInterval,
                              cmdOpts,
                              exitStatus)) {
            return exitStatus;
        }

        execBenchmark(pSdk, std::move(pSceneCtx));
    } else {
        if (!createNewSession(*pSdk,
                              *pSceneCtx,
                              sessionName,
                              numMcrtMin,
                              numMcrtMax,
                              aovInterval,
                              cmdOpts,
                              exitStatus)) {
            return exitStatus;
        }

        // not in gui mode just sleep the main thread until we are done
        // or something bad happened
        while(!frameWritten && pSdk->isConnected() && !arrasExceptionThrown && !arrasStopped) {
            sleep(1);
        }
    }

    if (pSdk->isConnected()) {
        if (!arrasExceptionThrown) {
            pSdk->sendMessage(mcrt::RenderMessages::createControlMessage(true));
        }

        pSdk->disconnect();
    }

    if (arrasExceptionThrown || arrasStopped) {
        exitStatus = 1;
    }

    return exitStatus;
}
