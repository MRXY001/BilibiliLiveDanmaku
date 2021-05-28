QT       += core gui network websockets multimedia multimediawidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS QT_MESSAGELOGCONTEXT HAVE_CONFIG_H
DEFINES += ENABLE_TEXTTOSPEECH

RC_FILE += resources/resource.rc

# 图片太大，会导致 cc1plus.exe:-1: error: out of memory allocating 4198399 bytes 错误
CONFIG += resources_big

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

win32{
DEFINES += ENABLE_SHORTCUT ENABLE_HTTP_SERVER ENABLE_TRAY
}
unix:!macx{
    DEFINES += ENABLE_SHORTCUT ENABLE_HTTP_SERVER ENABLE_TRAY
}

contains(DEFINES, ENABLE_SHORTCUT) {
    include($$PWD/third_party/qxtglobalshortcut5/qxt.pri)
}else{
    message("shortcuts not support")
}

contains(DEFINES, ENABLE_TEXTTOSPEECH) {
    QT += texttospeech
}

INCLUDEPATH += \
    global/ \
    mainwindow/ \
    third_party/utils/ \
    mainwindow/list_items/ \
    mainwindow/live_danmaku/ \
    third_party/interactive_buttons/ \
    third_party/facile_menu/ \
    third_party/qhttpserver/ \
    order_player/ \
    third_party/color_octree/ \
    widgets/lucky_draw/ \
    widgets/video_player/ \
    widgets/catch_you_dialog/ \
    widgets/eternal_block_dialog/ \
    widgets/login_dialog/ \
    widgets/room_status_dialog/ \
    widgets/video_lyric_creator/ \
    widgets/guard_online/ \
    widgets/smooth_scroll/ \
    widgets/ \
    third_party/ \
    widgets/editor/ \
    third_party/gif/ \
    third_party/picture_browser/

SOURCES += \
    global/runtime.cpp \
    global/usettings.cpp \
    mainwindow/basewindow.cpp \
    mainwindow/mainwindow.cpp \
    third_party/color_octree/coloroctree.cpp \
    third_party/color_octree/imageutil.cpp \
    third_party/facile_menu/facilemenu.cpp \
    third_party/facile_menu/facilemenuitem.cpp \
    third_party/gif/avilib.cpp \
    third_party/gif/gif.cpp \
    third_party/interactive_buttons/interactivebuttonbase.cpp \
    mainwindow/list_items/listiteminterface.cpp \
    mainwindow/live_danmaku/livedanmakuwindow.cpp \
    widgets/guard_online/guardonlinedialog.cpp \
    widgets/lucky_draw/luckydrawwindow.cpp \
    mainwindow/main.cpp \
    order_player/desktoplyricwidget.cpp \
    order_player/logindialog.cpp \
    order_player/numberanimation.cpp \
    order_player/orderplayerwindow.cpp \
    third_party/picture_browser/picturebrowser.cpp \
    third_party/picture_browser/resizablepicture.cpp \
    third_party/qrencode/bitstream.c \
    third_party/qrencode/mask.c \
    third_party/qrencode/mmask.c \
    third_party/qrencode/mqrspec.c \
    third_party/qrencode/qrencode.c \
    third_party/qrencode/qrinput.c \
    third_party/qrencode/qrspec.c \
    third_party/qrencode/rsecc.c \
    third_party/qrencode/split.c \
    mainwindow/server.cpp \
    third_party/utils/xfytts.cpp \
    widgets/smooth_scroll/smoothlistwidget.cpp \
    widgets/video_player/videosurface.cpp \
    widgets/catch_you_dialog/catchyouwidget.cpp \
    widgets/editor/conditioneditor.cpp \
    widgets/escape_dialog/escapedialog.cpp \
    widgets/escape_dialog/hoverbutton.cpp \
    widgets/eternal_block_dialog/eternalblockdialog.cpp \
    mainwindow/list_items/eventwidget.cpp \
    widgets/fluentbutton.cpp \
    widgets/mytabwidget.cpp \
    widgets/login_dialog/qrcodelogindialog.cpp \
    mainwindow/list_items/replywidget.cpp \
    widgets/room_status_dialog/roomstatusdialog.cpp \
    mainwindow/list_items/taskwidget.cpp \
    third_party/utils/fileutil.cpp \
    third_party/utils/stringutil.cpp \
    third_party/utils/textinputdialog.cpp \
    widgets/video_player/livevideoplayer.cpp \
    widgets/video_lyric_creator/videolyricscreator.cpp

HEADERS += \
    global/defines.h \
    global/runtime.h \
    global/signaltransfer.h \
    global/usettings.h \
    mainwindow/basewindow.h \
    mainwindow/mainwindow.h \
    third_party/color_octree/coloroctree.h \
    third_party/color_octree/imageutil.h \
    third_party/facile_menu/facilemenu.h \
    third_party/facile_menu/facilemenuitem.h \
    third_party/gif/avilib.h \
    third_party/gif/gif.h \
    third_party/interactive_buttons/interactivebuttonbase.h \
    mainwindow/list_items/listiteminterface.h \
    mainwindow/live_danmaku/commonvalues.h \
    mainwindow/live_danmaku/freecopyedit.h \
    mainwindow/live_danmaku/livedanmakuwindow.h \
    mainwindow/live_danmaku/livedanmaku.h \
    mainwindow/live_danmaku/portraitlabel.h \
    third_party/utils/mysettings.h \
    widgets/guard_online/guardonlinedialog.h \
    widgets/lucky_draw/luckydrawwindow.h \
    order_player/clickslider.h \
    order_player/desktoplyricwidget.h \
    order_player/itemselectionlistview.h \
    order_player/logindialog.h \
    order_player/lyricstreamwidget.h \
    order_player/numberanimation.h \
    order_player/orderplayerwindow.h \
    order_player/roundedpixmaplabel.h \
    order_player/songbeans.h \
    third_party/picture_browser/ASCII_Art.h \
    third_party/picture_browser/picturebrowser.h \
    third_party/picture_browser/resizablepicture.h \
    third_party/qrencode/bitstream.h \
    third_party/qrencode/config.h \
    third_party/qrencode/mask.h \
    third_party/qrencode/mmask.h \
    third_party/qrencode/mqrspec.h \
    third_party/qrencode/qrencode.h \
    third_party/qrencode/qrencode_inner.h \
    third_party/qrencode/qrinput.h \
    third_party/qrencode/qrspec.h \
    third_party/qrencode/rsecc.h \
    third_party/qrencode/split.h \
    third_party/utils/myjson.h \
    third_party/utils/xfytts.h \
    widgets/smooth_scroll/smoothlistwidget.h \
    widgets/smooth_scroll/smoothscrollbean.h \
    widgets/video_player/videosurface.h \
    widgets/RoundedAnimationLabel.h \
    widgets/catch_you_dialog/catchyouwidget.h \
    widgets/editor/conditioneditor.h \
    widgets/escape_dialog/escapedialog.h \
    widgets/escape_dialog/hoverbutton.h \
    widgets/eternal_block_dialog/eternalblockdialog.h \
    mainwindow/list_items/eventwidget.h \
    widgets/fluentbutton.h \
    widgets/mytabwidget.h \
    widgets/netinterface.h \
    widgets/login_dialog/qrcodelogindialog.h \
    mainwindow/list_items/replywidget.h \
    widgets/room_status_dialog/roomstatusdialog.h \
    mainwindow/list_items/taskwidget.h \
    third_party/utils/dlog.h \
    third_party/utils/fileutil.h \
    third_party/utils/netutil.h \
    third_party/utils/pinyinutil.h \
    third_party/utils/stringutil.h \
    third_party/utils/textinputdialog.h \
    widgets/video_player/livevideoplayer.h \
    widgets/video_lyric_creator/videolyricscreator.h

FORMS += \
    mainwindow/basewindow.ui \
    mainwindow/mainwindow.ui \
    widgets/guard_online/guardonlinedialog.ui \
    widgets/lucky_draw/luckydrawwindow.ui \
    order_player/logindialog.ui \
    order_player/orderplayerwindow.ui \
    third_party/picture_browser/picturebrowser.ui \
    third_party/utils/textinputdialog.ui \
    widgets/video_player/livevideoplayer.ui \
    widgets/catch_you_dialog/catchyouwidget.ui \
    widgets/eternal_block_dialog/eternalblockdialog.ui \
    widgets/login_dialog/qrcodelogindialog.ui \
    widgets/room_status_dialog/roomstatusdialog.ui \
    widgets/video_lyric_creator/videolyricscreator.ui

contains(DEFINES, ENABLE_HTTP_SERVER) {
HEADERS += \
    third_party/qhttpserver/qhttpconnection.h \
    third_party/qhttpserver/qhttprequest.h \
    third_party/qhttpserver/qhttpresponse.h \
    third_party/qhttpserver/qhttpserver.h \
    third_party/qhttpserver/qhttpserverapi.h \
    third_party/qhttpserver/qhttpserverfwd.h
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    resources/resource.rc \
    Examples.md \
    README.md \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew \
    android/gradlew.bat \
    android/res/values/libs.xml \
    resources/icons/appicon.ico \
    pictures/神奇弹幕-用法.pdf \
    resource.rc \
    resources/LAVFilters-0.74.1-Installer.exe \
    resources/点歌姬-用法.pdf \
    resources/神奇弹幕-用法.pdf

RESOURCES += \
    resources/resource.qrc

contains(ANDROID_TARGET_ARCH,) {
    ANDROID_PACKAGE_SOURCE_DIR = \
        $$PWD/android
}


unix|win32: LIBS += -L$$PWD/third_party/libs/ -lqhttpserver
win32: LIBS += -lversion

INCLUDEPATH += $$PWD/third_party/libs \
    qhttpserver/
DEPENDPATH += $$PWD/third_party/libs
