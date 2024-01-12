// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "DebugConsoleSetup.h"
#include "ImageView.h"

#include <mcrt_messages/RenderMessages.h>
#include <mcrt_messages/ViewportMessage.h>

namespace arras_render {

void
debugConsoleSetup(int port,
                  std::shared_ptr<arras4::sdk::SDK> &sdk,
                  std::shared_ptr<mcrt_dataio::ClientReceiverFb> &fbReceiver,
                  std::atomic<ImageView *> &imageView)
{
    std::cout << "debug-console port:" << port << '\n';
    fbReceiver->consoleEnable(static_cast<unsigned short>(port),
                              [&](const arras4::api::MessageContentConstPtr msg) -> bool {
                                  sdk->sendMessage(msg);
                                  return true;
                              });
    /* useful for debug autoSetup mode
    fbReceiver->consoleAutoSetup([&](const arras4::api::MessageContentConstPtr msg) -> bool {
            sdk->sendMessage(msg);
            return true;
        });
    */

    //------------------------------
    //
    // arras_render specific command configuration for ClientReceiverFb console driver.
    // Regarding Parser/Arg detail info, see scene_rdl2::grid_util::{Parser,Arg}.
    // Regarding base functionality of ClientReceiverConsoleDriver,
    // see mcrt_dataio::ClientReceiverConsoleDriver::parserConfigure().
    //            
    static scene_rdl2::grid_util::Parser sParserImageView;
    static scene_rdl2::grid_util::Parser sParserRoi;
    static scene_rdl2::grid_util::Parser sParserViewport;

    using Arg = scene_rdl2::grid_util::Arg;

    mcrt_dataio::ClientReceiverConsoleDriver &console = fbReceiver->consoleDriver();
    scene_rdl2::grid_util::Parser& parser = console.getRootParser();
    parser.opt("imageView", "...command...", "imageView command",
               [&](Arg &arg) -> bool { return sParserImageView.main(arg.childArg()); });
    sParserImageView.opt("roi", "...command...", "ROI command",
                         [&](Arg &arg) -> bool { return sParserRoi.main(arg.childArg()); });
    sParserImageView.opt("viewport", "...command...", "viewport command",
                         [&](Arg &arg) -> bool { return sParserViewport.main(arg.childArg()); });
    sParserImageView.opt("camPlayback", "...command...", "camera playback command",
                         [&](Arg& arg) -> bool {
                             if (!imageView.load()) {
                                 return arg.msg("mImageView is null, no cam playback information yet\n");                                  
                             }
                             return imageView.load()->getCamPlayback().getParser().main(arg.childArg());
                         });
    sParserImageView.opt("overlay", "<offX> <offY> <fontSize>", "set overlay offset and size",
                         [&](Arg& arg) -> bool {
                             unsigned offX = arg.as<unsigned>(0);
                             unsigned offY = arg.as<unsigned>(1);
                             unsigned fontSize = arg.as<unsigned>(2);
                             arg += 3;
                             if (!imageView.load()) {
                                 return arg.msg("mImageView is null, no cam playback information yet\n");
                             }
                             imageView.load()->setOverlayParam(offX, offY, fontSize);
                             return true;
                         });
    sParserImageView.opt("showImgPos", "", "show image display screen pixel position",
                         [&](Arg& arg) -> bool {
                             if (!imageView.load()) {
                                 return arg.msg("mImageView is null, no cam playback information yet\n");                                  
                             }
                             int x, y;
                             imageView.load()->getImageDisplayWidgetPos(x, y);
                             std::ostringstream ostr;
                             ostr << ":0.0+" << x << "," << y;
                             return arg.msg(ostr.str() + '\n');
                         });

    //------------------------------

    parser.opt("display", "", "display current data",
               [&](Arg& arg) -> bool {
                   if (!imageView.load()) { return arg.msg("mImageView is null\n"); }
                   imageView.load()->displayFrame();
                   return true;
               });
    parser.opt("clear", "", "clear display",
               [&](Arg& arg) -> bool {
                   if (!imageView.load()) { return arg.msg("mImageView is null\n"); }
                   imageView.load()->clearDisplayFrame();
                   return true;
               });

    // test command
    parser.opt("sendWholeScene", "", "send entire scene w/ forceReload flag",
               [&](Arg &) -> bool {
                   imageView.load()->sendCommand("sendWholeScene");
                   return true;
               });

    //------------------------------

    sParserRoi.description("ROI command");
    sParserRoi.opt("on", "<xMin> <yMin> <xMax> <yMax>", "enable ROI window",
                   [&](Arg &arg) -> bool {
                       int xMin = arg.as<int>(0);
                       int yMin = arg.as<int>(1);
                       int xMax = arg.as<int>(2);
                       int yMax = arg.as<int>(3);
                       arg += 4;
                       imageView.load()->changeROI(xMin, yMin, xMax, yMax);
                       return console.sendMessage([&]() -> const arras4::api::MessageContentConstPtr {
                               return mcrt::RenderMessages::createRoiMessage(xMin, yMin, xMax, yMax);
                           });
                   });
    sParserRoi.opt("off", "", "disable ROI window",
                   [&](Arg &arg) -> bool {
                       imageView.load()->changeROIoff();
                       return console.sendMessage([&]() -> const arras4::api::MessageContentConstPtr {
                               return mcrt::RenderMessages::createRoiStatusMessage(false);                              
                           });
                   });
    sParserRoi.opt("show", "", "show current ROI info",
                   [&](Arg &arg) -> bool {
                       if (!imageView.load()) {
                           return arg.msg("mImageView is null, no viewport information yet\n");
                       }
                       const scene_rdl2::rdl2::SceneContext *sceneContext = imageView.load()->getSceneContext();
                       if (!sceneContext) {
                           return arg.msg("sceneContext is null, no viewport information yet\n");
                       }

                       const scene_rdl2::rdl2::SceneVariables &sceneVars = sceneContext->getSceneVariables();
                       scene_rdl2::math::HalfOpenViewport currViewport;
                       if (!sceneVars.getSubViewport(currViewport)) {
                           return arg.msg("no subViewport (ROI)\n");
                       }

                       std::ostringstream ostr;
                       ostr << "subViewport(ROI) = ("
                            << currViewport.mMinX << ", " << currViewport.mMinY << ", "
                            << currViewport.mMaxX << ", " << currViewport.mMaxY << ") ("
                            << currViewport.width() << " x " << currViewport.height() << ")";
                       return arg.msg(ostr.str() + '\n');
                   });
    
    //------------------------------

    sParserViewport.description("viewport command");
    sParserViewport.opt("set", "<w> <h>", "change iage width and height",
                        [&](Arg &arg) -> bool {
                            int width = arg.as<int>(0);
                            int height = arg.as<int>(1);
                            arg += 2;
                            imageView.load()->changeImageSize(width, height);
                            return console.sendMessage([&]() -> const arras4::api::MessageContentConstPtr {
                                    return std::make_shared<mcrt::ViewportMessage>(0, 0, width, height);
                                });
                        });
    sParserViewport.opt("show", "", "show current image width and height",
                        [&](Arg &arg) -> bool {
                            if (!imageView.load()) {
                                return arg.msg("mImageView is null, no viewport information yet\n");
                            }
                            const scene_rdl2::rdl2::SceneContext *sceneContext = imageView.load()->getSceneContext();
                            if (!sceneContext) {
                                return arg.msg("sceneContext is null, no viewport information yet\n");
                            }

                            const scene_rdl2::rdl2::SceneVariables &sceneVars = sceneContext->getSceneVariables();
                            int imageWidth = sceneVars.get(scene_rdl2::rdl2::SceneVariables::sImageWidth);
                            int imageHeight = sceneVars.get(scene_rdl2::rdl2::SceneVariables::sImageHeight);

                            std::ostringstream ostr;
                            ostr << "imageWidth:" << imageWidth << " imageHeight:" << imageHeight;
                            return arg.msg(ostr.str() + '\n');
                        });
}

} // namespace arras_render
