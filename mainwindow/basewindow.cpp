#include <zlib.h>
#include <QListView>
#include <QMovie>
#include <QClipboard>
#include "basewindow.h"
#include "ui_mainwindow.h"
#include "videolyricscreator.h"
#include "roomstatusdialog.h"
#include "RoundedAnimationLabel.h"
#include "catchyouwidget.h"
#include "qrcodelogindialog.h"
#include "fileutil.h"
#include "stringutil.h"
#include "escape_dialog/escapedialog.h"
#include "guardonlinedialog.h"

QHash<qint64, QString> CommonValues::localNicknames; // 本地昵称
QHash<qint64, qint64> CommonValues::userComeTimes;   // 用户进来的时间（客户端时间戳为准）
QHash<qint64, qint64> CommonValues::userBlockIds;    // 本次用户屏蔽的ID
QSettings* CommonValues::danmakuCounts = nullptr;    // 每个用户的统计
QSettings* CommonValues::userMarks = nullptr;        // 每个用户的备注
QList<LiveDanmaku> CommonValues::allDanmakus;        // 本次启动的所有弹幕
QList<qint64> CommonValues::careUsers;               // 特别关心
QList<qint64> CommonValues::strongNotifyUsers;       // 强提醒
QHash<QString, QString> CommonValues::pinyinMap;     // 拼音
QList<QPair<QString, QString>> CommonValues::customVariant; // 自定义变量
QList<QPair<QString, QString>> CommonValues::variantTranslation; // 变量翻译
QList<qint64> CommonValues::notWelcomeUsers;         // 不自动欢迎
QList<qint64> CommonValues::notReplyUsers;           // 不自动回复
QHash<int, QString> CommonValues::giftNames;         // 自定义礼物名字
QList<EternalBlockUser> CommonValues::eternalBlockUsers; // 永久禁言
QHash<qint64, QString> CommonValues::currentGuards;  // 当前船员
QString CommonValues::browserCookie;
QString CommonValues::browserData;
QString CommonValues::csrf_token;
QVariant CommonValues::userCookies;

BaseWindow::BaseWindow(QWidget *parent)
    : QMainWindow(parent),
      NetInterface(this),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    QApplication::setQuitOnLastWindowClosed(false);
    connect(qApp, &QApplication::paletteChanged, this, [=](const QPalette& pa){
        ui->tabWidget->setPalette(pa);
    });
    ui->menubar->setStyleSheet("QMenuBar:item{background:transparent;}QMenuBar{background:transparent;}");

    // 隐藏用不到的工具
    ui->pushNextCmdButton->hide();
    ui->timerPushCmdCheck->hide();
    ui->timerPushCmdSpin->hide();
    ui->closeTransMouseButton->hide();
    ui->pkMelonValButton->hide();
    ui->AIReplyIdButton->hide();
    ui->AIReplyKeyButton->hide();

    // 限制
    ui->roomIdEdit->setValidator(new QRegExpValidator(QRegExp("[0-9]+$")));

    // 路径
    initPath();
    bool firstOpen = !QFileInfo(dataPath + "settings.ini").exists();
    settings = new QSettings(dataPath + "settings.ini", QSettings::Format::IniFormat);
    heaps = new QSettings(dataPath + "heaps.ini", QSettings::Format::IniFormat);
    robotRecord = new QSettings(dataPath + "robots.ini", QSettings::Format::IniFormat);
    wwwDir = QDir(dataPath + "www");

    appVersion = GetFileVertion(QApplication::applicationFilePath()).trimmed();
    if (appVersion.startsWith("v") || appVersion.startsWith("V"))
            appVersion.replace(0, 1, "");
    if (appVersion != settings->value("runtime/appVersion").toString())
    {
        upgradeVersionToLastest(settings->value("runtime/appVersion").toString());
        settings->setValue("runtime/appVersion", appVersion);
    }

    // 页面
    int tabIndex = settings->value("mainwindow/tabIndex", 0).toInt();
    if (tabIndex >= 0 && tabIndex < ui->tabWidget->count())
        ui->tabWidget->setCurrentIndex(tabIndex);

    // 房间号
    roomId = settings->value("danmaku/roomId", "").toString();
    if (!roomId.isEmpty())
        ui->roomIdEdit->setText(roomId);

    // 移除间隔
    removeTimer = new QTimer(this);
    removeTimer->setInterval(200);
    connect(removeTimer, SIGNAL(timeout()), this, SLOT(removeTimeoutDanmaku()));
    removeTimer->start();

    int removeIv = settings->value("danmaku/removeInterval", 60).toInt();
    ui->removeDanmakuIntervalSpin->setValue(removeIv); // 自动引发改变事件
    this->removeDanmakuInterval = removeIv * 1000;

    removeIv = settings->value("danmaku/removeTipInterval", 20).toInt();
    ui->removeDanmakuTipIntervalSpin->setValue(removeIv); // 自动引发改变事件
    this->removeDanmakuTipInterval = removeIv * 1000;

    // 单条弹幕最长长度
    danmuLongest = settings->value("danmaku/danmuLongest", 20).toInt();
    ui->danmuLongestSpin->setValue(danmuLongest);

    // 失败重试
    ui->retryFailedDanmuCheck->setChecked(settings->value("danmaku/retryFailedDanmu", true).toBool());

    // 发送队列
    autoMsgTimer = new QTimer(this) ;
    autoMsgTimer->setInterval(1500); // 1.5秒发一次弹幕
    connect(autoMsgTimer, &QTimer::timeout, this, [=]{
        slotSendAutoMsg(true);
    });

    // 点歌自动复制
    diangeAutoCopy = settings->value("danmaku/diangeAutoCopy", true).toBool();
    ui->DiangeAutoCopyCheck->setChecked(diangeAutoCopy);
    ui->diangeNeedMedalCheck->setChecked(settings->value("danmaku/diangeNeedMedal", true).toBool());
    QString defaultDiangeFormat = "^点歌[ :：,，]+(.+)";
    diangeFormatString = settings->value("danmaku/diangeFormat", defaultDiangeFormat).toString();
    ui->diangeFormatEdit->setText(diangeFormatString);
    connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), this, SLOT(slotDiange(LiveDanmaku)));
    ui->diangeReplyCheck->setChecked(settings->value("danmaku/diangeReply", false).toBool());
    ui->diangeShuaCheck->setChecked(settings->value("danmaku/diangeShua", true).toBool());
    ui->autoPauseOuterMusicCheck->setChecked(settings->value("danmaku/autoPauseOuterMusic", false).toBool());
    ui->outerMusicKeyEdit->setText(settings->value("danmaku/outerMusicPauseKey").toString());
    ui->orderSongsToFileCheck->setChecked(settings->value("danmaku/orderSongsToFile", false).toBool());
    ui->orderSongsToFileFormatEdit->setText(settings->value("danmaku/orderSongsToFileFormat", "{歌名} - {歌手}").toString());
    ui->orderSongsToFileMaxSpin->setValue(settings->value("danmaku/orderSongsToFileMax", 9).toInt());
    ui->songLyricsToFileCheck->setChecked(settings->value("danmaku/songLyricsToFile", false).toBool());
    ui->songLyricsToFileMaxSpin->setValue(settings->value("danmaku/songLyricsToFileMax", 2).toInt());

    // 自动翻译
    bool trans = settings->value("danmaku/autoTrans", true).toBool();
    ui->languageAutoTranslateCheck->setChecked(trans);

    // 自动回复
    bool reply = settings->value("danmaku/aiReply", false).toBool();
    ui->AIReplyCheck->setChecked(reply);
    ui->AIReplyMsgCheck->setCheckState(static_cast<Qt::CheckState>(settings->value("danmaku/aiReplyMsg", 0).toInt()));
    ui->AIReplyMsgCheck->setEnabled(reply);

    // 黑名单管理
    ui->enableBlockCheck->setChecked(settings->value("block/enableBlock", false).toBool());

    // 新人提示
    ui->newbieTipCheck->setChecked(settings->value("block/newbieTip", true).toBool());

    // 自动禁言
    ui->autoBlockNewbieCheck->setChecked(settings->value("block/autoBlockNewbie", false).toBool());
    ui->autoBlockNewbieKeysEdit->setPlainText(settings->value("block/autoBlockNewbieKeys").toString());

    ui->autoBlockNewbieNotifyCheck->setChecked(settings->value("block/autoBlockNewbieNotify", false).toBool());
    ui->autoBlockNewbieNotifyWordsEdit->setPlainText(settings->value("block/autoBlockNewbieNotifyWords").toString());
    ui->autoBlockNewbieNotifyCheck->setEnabled(ui->autoBlockNewbieCheck->isChecked());

    ui->promptBlockNewbieCheck->setChecked(settings->value("block/promptBlockNewbie", false).toBool());
    ui->promptBlockNewbieKeysEdit->setPlainText(settings->value("block/promptBlockNewbieKeys").toString());

    ui->notOnlyNewbieCheck->setChecked(settings->value("block/notOnlyNewbie", false).toBool());
    ui->blockNotOnlyNewbieCheck->setChecked(settings->value("block/blockNotOnlyNewbieCheck", false).toBool());

    ui->autoBlockTimeSpin->setValue(settings->value("block/autoTime", 1).toInt());

    // 实时弹幕
    if (settings->value("danmaku/liveWindow", false).toBool())
         on_actionShow_Live_Danmaku_triggered();

    // 点歌姬
    if (settings->value("danmaku/playerWindow", false).toBool())
        on_actionShow_Order_Player_Window_triggered();

    // 录播
    if (settings->value("danmaku/record", false).toBool())
        ui->recordCheck->setChecked(true);
    int recordSplit = settings->value("danmaku/recordSplit", 30).toInt();
    ui->recordSplitSpin->setValue(recordSplit);
    recordTimer = new QTimer(this);
    recordTimer->setInterval(recordSplit * 60000); // 默认30分钟断开一次
    connect(recordTimer, &QTimer::timeout, this, [=]{
        if (!recordLoop) // 没有正在录制
            return ;

        recordLoop->quit(); // 这个就是停止录制了
        // 停止之后，录播会检测是否还需要重新录播
        // 如果是，则继续录
    });
    ui->recordCheck->setToolTip("保存地址：" + dataPath + "record/房间名_时间.mp4");

    // 发送弹幕
    browserCookie = settings->value("danmaku/browserCookie", "").toString();
    browserData = settings->value("danmaku/browserData", "").toString();
    int posl = browserCookie.indexOf("bili_jct=") + 9;
    int posr = browserCookie.indexOf(";", posl);
    if (posr == -1) posr = browserCookie.length();
    csrf_token = browserCookie.mid(posl, posr - posl);
    userCookies = getCookies();
    getUserInfo();

    // 保存弹幕
    bool saveDanmuToFile = settings->value("danmaku/saveDanmakuToFile", false).toBool();
    if (saveDanmuToFile)
        ui->saveDanmakuToFileCheck->setChecked(true);

    // 每日数据
    bool calcDaliy = settings->value("live/calculateDaliyData", true).toBool();
    ui->calculateDailyDataCheck->setChecked(calcDaliy);
    if (calcDaliy)
        startCalculateDailyData();

    // PK串门提示
    pkChuanmenEnable = settings->value("pk/chuanmen", false).toBool();
    ui->pkChuanmenCheck->setChecked(pkChuanmenEnable);

    // PK消息同步
    pkMsgSync = settings->value("pk/msgSync", 0).toInt();
    if (pkMsgSync == 0)
        ui->pkMsgSyncCheck->setCheckState(Qt::Unchecked);
    else if (pkMsgSync == 1)
        ui->pkMsgSyncCheck->setCheckState(Qt::PartiallyChecked);
    else if (pkMsgSync == 2)
        ui->pkMsgSyncCheck->setCheckState(Qt::Checked);
    ui->pkMsgSyncCheck->setText(pkMsgSync == 1 ? "PK同步消息(仅视频)" : "PK同步消息");
    ui->pkMsgSyncCheck->setEnabled(pkChuanmenEnable);

    // 判断机器人
    judgeRobot = settings->value("danmaku/judgeRobot", 0).toInt();
    ui->judgeRobotCheck->setCheckState((Qt::CheckState)judgeRobot);
    ui->judgeRobotCheck->setText(judgeRobot == 1 ? "机器人判断(仅关注)" : "机器人判断");

    // 本地昵称
    QStringList namePares = settings->value("danmaku/localNicknames").toString().split(";", QString::SkipEmptyParts);
    foreach (QString pare, namePares)
    {
        QStringList sl = pare.split("=>");
        if (sl.size() < 2)
            continue;

        localNicknames.insert(sl.at(0).toLongLong(), sl.at(1));
    }

    // 礼物别名
    namePares = settings->value("danmaku/giftNames").toString().split(";", QString::SkipEmptyParts);
    foreach (QString pare, namePares)
    {
        QStringList sl = pare.split("=>");
        if (sl.size() < 2)
            continue;

        giftNames.insert(sl.at(0).toInt(), sl.at(1));
    }

    // 特别关心
    QStringList usersS = settings->value("danmaku/careUsers", "20285041").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersS)
    {
        careUsers.append(s.toLongLong());
    }

    // 强提醒
    QStringList usersSN = settings->value("danmaku/strongNotifyUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersSN)
    {
        strongNotifyUsers.append(s.toLongLong());
    }

    // 不自动欢迎
    QStringList usersNW = settings->value("danmaku/notWelcomeUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersNW)
    {
        notWelcomeUsers.append(s.toLongLong());
    }

    // 不自动回复
    QStringList usersNR = settings->value("danmaku/notReplyUsers", "").toString().split(";", QString::SkipEmptyParts);
    foreach (QString s, usersNR)
    {
        notReplyUsers.append(s.toLongLong());
    }

    // 礼物连击
    ui->giftComboSendCheck->setChecked(settings->value("danmaku/giftComboSend", false).toBool());
    ui->giftComboDelaySpin->setValue(settings->value("danmaku/giftComboDelay",  5).toInt());
    ui->giftComboTopCheck->setChecked(settings->value("danmaku/giftComboTop", false).toBool());
    ui->giftComboMergeCheck->setChecked(settings->value("danmaku/giftComboMerge", false).toBool());
    comboTimer = new QTimer(this);
    comboTimer->setInterval(500);
    connect(comboTimer, SIGNAL(timeout()), this, SLOT(slotComboSend()));

    // 仅开播发送
    ui->sendAutoOnlyLiveCheck->setChecked(settings->value("danmaku/sendAutoOnlyLive", true).toBool());
    ui->autoDoSignCheck->setChecked(settings->value("danmaku/autoDoSign", false).toBool());

    // 勋章升级
    ui->listenMedalUpgradeCheck->setChecked(settings->value("danmaku/listenMedalUpgrade", false).toBool());

    // 弹幕次数
    danmakuCounts = new QSettings(dataPath+"danmu_count.ini", QSettings::Format::IniFormat);

    // 用户备注
    userMarks = new QSettings(dataPath+"user_mark.ini", QSettings::Format::IniFormat);

    // 状态栏
    statusLabel = new QLabel(this);
    fansLabel = new QLabel(this);
    rankLabel = new QLabel(this);
    this->statusBar()->addWidget(statusLabel, 1);
    this->statusBar()->addWidget(fansLabel, 1);
    this->statusBar()->addWidget(rankLabel, 1);
    statusLabel->setAlignment(Qt::AlignLeft);
    fansLabel->setAlignment(Qt::AlignCenter);
    rankLabel->setAlignment(Qt::AlignRight);

    // 托盘
    tray = new QSystemTrayIcon(this);//初始化托盘对象tray
    tray->setIcon(QIcon(QPixmap(":/icons/star")));//设定托盘图标，引号内是自定义的png图片路径
    tray->setToolTip("神奇弹幕");
    tray->show();//让托盘图标显示在系统托盘上
    QString title="APP Message";
    QString text="神奇弹幕";
//    tray->showMessage(title,text,QSystemTrayIcon::Information,3000); //最后一个参数为提示时长，默认10000，即10s

    QAction *windowAction = new QAction(QIcon(":/icons/star"), "主界面", this);
    connect(windowAction, SIGNAL(triggered()), this, SLOT(show()));
    QAction *liveDanmakuAction = new QAction(QIcon(":/icons/danmu"), "弹幕姬", this);
    connect(liveDanmakuAction, SIGNAL(triggered()), this, SLOT(on_actionShow_Live_Danmaku_triggered()));
    QAction *orderPlayerAction = new QAction(QIcon(":/icons/order_song"), "点歌姬", this);
    connect(orderPlayerAction, SIGNAL(triggered()), this, SLOT(on_actionShow_Order_Player_Window_triggered()));
    QAction *videoAction = new QAction(QIcon(":/icons/live"), "视频流", this);
    connect(videoAction, SIGNAL(triggered()), this, SLOT(on_actionShow_Live_Video_triggered()));
    QAction *quitAction = new QAction(QIcon(":/icons/cry"), "退出", this);
    connect(quitAction, SIGNAL(triggered()), this, SLOT(prepareQuit()));

    trayMenu = new QMenu(this);
    trayMenu->addAction(windowAction);
    trayMenu->addAction(liveDanmakuAction);
    trayMenu->addAction(orderPlayerAction);
    trayMenu->addAction(videoAction);
    trayMenu->addSeparator();
    trayMenu->addAction(quitAction);
    tray->setContextMenu(trayMenu);

    connect(tray,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),this,SLOT(showWidget(QSystemTrayIcon::ActivationReason)));

    // 大乱斗
    pkTimer = new QTimer(this);
    connect(pkTimer, &QTimer::timeout, this, [=]{
        // 更新PK信息
        int second = 0;
        int minute = 0;
        if (pkEndTime)
        {
            second = static_cast<int>(pkEndTime - QDateTime::currentSecsSinceEpoch());
            if (second < 0) // 结束后会继续等待一段时间，这时候会变成负数
                second = 0;
            minute = second / 60;
            second = second % 60;
        }
        QString text = QString("%1:%2 %3/%4")
                .arg(minute)
                .arg((second < 10 ? "0" : "") + QString::number(second))
                .arg(myVotes)
                .arg(matchVotes);
        if (danmakuWindow)
            danmakuWindow->setStatusText(text);
    });
    pkTimer->setInterval(300);

    // 大乱斗自动赠送吃瓜
    bool melon = settings->value("pk/autoMelon", false).toBool();
    ui->pkAutoMelonCheck->setChecked(melon);
    pkMaxGold = settings->value("pk/maxGold", 300).toInt();
    pkJudgeEarly = settings->value("pk/judgeEarly", 2000).toInt();
    toutaCount = settings->value("pk/toutaCount", 0).toInt();
    chiguaCount = settings->value("pk/chiguaCount", 0).toInt();
    goldTransPk = settings->value("pk/goldTransPk", goldTransPk).toInt();
    toutaBlankList = settings->value("pk/blankList").toString().split(";");
    ui->pkAutoMaxGoldCheck->setChecked(settings->value("pk/autoMaxGold", true).toBool());

    // 自定义变量
    restoreCustomVariant(settings->value("danmaku/customVariant", "").toString());

    // 多语言翻译
    restoreVariantTranslation();

    // 定时任务
    srand((unsigned)time(0));
    restoreTaskList();

    // 自动回复
    restoreReplyList();

    // 事件动作
    restoreEventList();

    // 保存舰长
    ui->saveEveryGuardCheck->setChecked(settings->value("danmaku/saveEveryGuard", false).toBool());
    ui->saveMonthGuardCheck->setChecked(settings->value("danmaku/saveMonthGuard", false).toBool());
    ui->saveEveryGiftCheck->setChecked(settings->value("danmaku/saveEveryGift", false).toBool());

    // 自动发送
    ui->autoSendWelcomeCheck->setChecked(settings->value("danmaku/sendWelcome", false).toBool());
    ui->autoSendGiftCheck->setChecked(settings->value("danmaku/sendGift", false).toBool());
    ui->autoSendAttentionCheck->setChecked(settings->value("danmaku/sendAttention", false).toBool());
    ui->sendWelcomeCDSpin->setValue(settings->value("danmaku/sendWelcomeCD", 10).toInt());
    ui->sendGiftCDSpin->setValue(settings->value("danmaku/sendGiftCD", 5).toInt());
    ui->sendAttentionCDSpin->setValue(settings->value("danmaku/sendAttentionCD", 5).toInt());
    ui->autoWelcomeWordsEdit->setPlainText(settings->value("danmaku/autoWelcomeWords", ui->autoWelcomeWordsEdit->toPlainText()).toString());
    ui->autoThankWordsEdit->setPlainText(settings->value("danmaku/autoThankWords", ui->autoThankWordsEdit->toPlainText()).toString());
    ui->autoAttentionWordsEdit->setPlainText(settings->value("danmaku/autoAttentionWords", ui->autoAttentionWordsEdit->toPlainText()).toString());
    ui->sendWelcomeTextCheck->setChecked(settings->value("danmaku/sendWelcomeText", true).toBool());
    ui->sendWelcomeVoiceCheck->setChecked(settings->value("danmaku/sendWelcomeVoice", false).toBool());
    ui->sendGiftTextCheck->setChecked(settings->value("danmaku/sendGiftText", true).toBool());
    ui->sendGiftVoiceCheck->setChecked(settings->value("danmaku/sendGiftVoice", false).toBool());
    ui->sendAttentionTextCheck->setChecked(settings->value("danmaku/sendAttentionText", true).toBool());
    ui->sendAttentionVoiceCheck->setChecked(settings->value("danmaku/sendAttentionVoice", false).toBool());
    ui->sendWelcomeTextCheck->setEnabled(ui->autoSendWelcomeCheck->isChecked());
    ui->sendWelcomeVoiceCheck->setEnabled(ui->autoSendWelcomeCheck->isChecked());
    ui->sendGiftTextCheck->setEnabled(ui->autoSendGiftCheck->isChecked());
    ui->sendGiftVoiceCheck->setEnabled(ui->autoSendGiftCheck->isChecked());
    ui->sendAttentionTextCheck->setEnabled(ui->autoSendAttentionCheck->isChecked());
    ui->sendAttentionVoiceCheck->setEnabled(ui->autoSendAttentionCheck->isChecked());

    // 文字转语音
    ui->autoSpeekDanmakuCheck->setChecked(settings->value("danmaku/autoSpeek", false).toBool());
    if (ui->sendWelcomeVoiceCheck->isChecked() || ui->sendGiftVoiceCheck->isChecked()
            || ui->sendAttentionVoiceCheck->isChecked() || ui->autoSpeekDanmakuCheck->isChecked())
        initTTS();

    voicePlatform = static_cast<VoicePlatform>(settings->value("voice/platform", 0).toInt());
    if (voicePlatform == VoiceLocal)
    {
        ui->voiceLocalRadio->setChecked(true);
        ui->voiceNameEdit->setText(settings->value("voice/localName").toString());
    }
    else if (voicePlatform == VoiceXfy)
    {
        ui->voiceXfyRadio->setChecked(true);
        ui->voiceNameEdit->setText(settings->value("xfytts/name").toString());
        ui->xfyAppIdEdit->setText(settings->value("xfytts/appid").toString());
        ui->xfyApiKeyEdit->setText(settings->value("xfytts/apikey").toString());
        ui->xfyApiSecretEdit->setText(settings->value("xfytts/apisecret").toString());
    }
    else if (voicePlatform == VoiceCustom)
    {
        ui->voiceCustomRadio->setChecked(true);
        ui->voiceNameEdit->setText(settings->value("voice/customName").toString());
    }

    ui->voicePitchSlider->setSliderPosition(settings->value("voice/pitch", 50).toInt());
    ui->voiceSpeedSlider->setSliderPosition(settings->value("voice/speed", 50).toInt());
    ui->voiceCustomUrlEdit->setText(settings->value("voice/customUrl", "").toString());

    // 开播
    ui->startLiveWordsEdit->setText(settings->value("live/startWords").toString());
    ui->endLiveWordsEdit->setText(settings->value("live/endWords").toString());
    ui->startLiveSendCheck->setChecked(settings->value("live/startSend").toBool());

    // 启动动画
    ui->startupAnimationCheck->setChecked(settings->value("mainwindow/splash", firstOpen).toBool());

    // 定时连接
    ui->timerConnectServerCheck->setChecked(settings->value("live/timerConnectServer", false).toBool());
    ui->startLiveHourSpin->setValue(settings->value("live/startLiveHour", 0).toInt());
    ui->endLiveHourSpin->setValue(settings->value("live/endLiveHour", 0).toInt());
    connectServerTimer = new QTimer(this);
    connectServerTimer->setInterval(CONNECT_SERVER_INTERVAL);
    connect(connectServerTimer, &QTimer::timeout, this, [=]{
        connectServerTimer->setInterval(900000); // 比如服务器主动断开，则会短期内重新定时，还原自动连接定时
        if (isLiving() && (socket->state() == QAbstractSocket::ConnectedState || socket->state() == QAbstractSocket::ConnectingState))
        {
            connectServerTimer->stop();
            return ;
        }
        startConnectRoom();
    });

    // WS连接
    initWS();
    startConnectRoom();

    // 10秒内不进行自动化操作
    QTimer::singleShot(3000, [=]{
        justStart = false;
    });

    // 读取拼音1
    QtConcurrent::run([=]{
        QFile pinyinFile(":/documents/pinyin");
        pinyinFile.open(QIODevice::ReadOnly);
        QTextStream pinyinIn(&pinyinFile);
        pinyinIn.setCodec("UTF-8");
        QString line = pinyinIn.readLine();
        while (!line.isNull())
        {
            if (!line.isEmpty())
            {
                QString han = line.at(0);
                QString pinyin = line.right(line.length()-1);
                pinyinMap.insert(han, pinyin);
            }
            line = pinyinIn.readLine();
        }
    });

    // 隐藏偷塔
    if (!settings->value("danmaku/touta", false).toBool())
    {
        ui->pkAutoMelonCheck->setText("此项禁止使用");
        ui->pkAutoMelonCheck->hide();
        ui->pkMaxGoldButton->hide();
        ui->pkMelonValButton->hide();
        ui->pkJudgeEarlyButton->hide();
        ui->pkBlankButton->hide();
        ui->pkAutoMaxGoldCheck->hide();
        ui->pkAutoMelonCheck->setChecked(false);
    }

    // 粉丝勋章
    ui->autoSwitchMedalCheck->setChecked(settings->value("danmaku/autoSwitchMedal", false).toBool());

    // 读取自定义快捷房间
    QStringList list = settings->value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
    ui->menu_3->addSeparator();
    for (int i = 0; i < list.size(); i++)
    {
        QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
        if (texts.size() < 1)
            continue ;
        QString id = texts.first();
        QString name = texts.size() >= 2 ? texts.at(1) : id;
        QAction* action = new QAction(name, this);
        ui->menu_3->addAction(action);
        connect(action, &QAction::triggered, this, [=]{
            ui->roomIdEdit->setText(id);
            on_roomIdEdit_editingFinished();
        });
    }

    // 滚屏
    ui->enableScreenDanmakuCheck->setChecked(settings->value("screendanmaku/enableDanmaku", false).toBool());
    ui->enableScreenMsgCheck->setChecked(settings->value("screendanmaku/enableMsg", false).toBool());
    ui->screenDanmakuLeftSpin->setValue(settings->value("screendanmaku/left", 0).toInt());
    ui->screenDanmakuRightSpin->setValue(settings->value("screendanmaku/right", 0).toInt());
    ui->screenDanmakuTopSpin->setValue(settings->value("screendanmaku/top", 10).toInt());
    ui->screenDanmakuBottomSpin->setValue(settings->value("screendanmaku/bottom", 60).toInt());
    ui->screenDanmakuSpeedSpin->setValue(settings->value("screendanmaku/speed", 10).toInt());
    ui->enableScreenMsgCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
    QString danmakuFontString = settings->value("screendanmaku/font").toString();
    if (!danmakuFontString.isEmpty())
        screenDanmakuFont.fromString(danmakuFontString);
    screenDanmakuColor = qvariant_cast<QColor>(settings->value("screendanmaku/color", QColor(0, 0, 0)));
    connect(this, &BaseWindow::signalNewDanmaku, this, [=](LiveDanmaku danmaku){
//        QtConcurrent::run([&]{
            showScreenDanmaku(danmaku);
//        });
    });

    connect(this, &BaseWindow::signalNewDanmaku, this, [=](LiveDanmaku danmaku){
        if (danmaku.isPkLink()) // 大乱斗对面的弹幕不朗读
            return ;
        if (ui->autoSpeekDanmakuCheck->isChecked() && danmaku.getMsgType() == MSG_DANMAKU)
            speakText(danmaku.getText());
    });

    // 自动签到
    if (settings->value("danmaku/autoDoSign", false).toBool())
    {
        ui->autoDoSignCheck->setChecked(true);
    }

    // 自动参与天选
    ui->autoLOTCheck->setChecked(settings->value("danmaku/autoLOT", false).toBool());

    // 自动获取小心心
    ui->acquireHeartCheck->setChecked(settings->value("danmaku/acquireHeart", false).toBool());

    // 自动赠送过期礼物
    ui->sendExpireGiftCheck->setChecked(settings->value("danmaku/sendExpireGift", false).toBool());

    // 永久禁言
    QJsonArray eternalBlockArray = settings->value("danmaku/eternalBlockUsers").toJsonArray();
    int eternalBlockSize = eternalBlockArray.size();
    for (int i = 0; i < eternalBlockSize; i++)
    {
        EternalBlockUser eb = EternalBlockUser::fromJson(eternalBlockArray.at(i).toObject());
        if (eb.uid && eb.roomId)
            eternalBlockUsers.append(eb);
    }

    // 开机自启
    if (settings->value("runtime/startOnReboot", false).toBool())
        ui->startOnRebootCheck->setChecked(true);

    // 每分钟定时
    minuteTimer = new QTimer(this);
    minuteTimer->setInterval(60000);
    connect(minuteTimer, &QTimer::timeout, this, [=]{
        // 直播间人气
        if (currentPopul > 1 && isLiving()) // 为0的时候不计入内；为1时可能机器人在线
        {
            sumPopul += currentPopul;
            countPopul++;

            dailyAvePopul = int(sumPopul / countPopul);
            if (dailySettings)
                dailySettings->setValue("average_popularity", dailyAvePopul);
        }
        if (dailyMaxPopul < currentPopul)
        {
            dailyMaxPopul = currentPopul;
            if (dailySettings)
                dailySettings->setValue("max_popularity", dailyMaxPopul);
        }

        // 弹幕人气
        danmuPopulValue += minuteDanmuPopul;
        danmuPopulQueue.append(minuteDanmuPopul);
        minuteDanmuPopul = 0;
        if (danmuPopulQueue.size() > 5)
            danmuPopulValue -= danmuPopulQueue.takeFirst();
        ui->popularityLabel->setToolTip("5分钟弹幕人气：" + snum(danmuPopulValue) + "，平均人气：" + snum(dailyAvePopul));

        triggerCmdEvent("DANMU_POPULARITY", LiveDanmaku());
    });

    // 每小时的事件
    hourTimer = new QTimer(this);
    hourTimer->setInterval(3600000);
    connect(hourTimer, &QTimer::timeout, this, [=]{
        // 永久禁言
        detectEternalBlockUsers();

        // 自动签到
        if (ui->autoDoSignCheck->isChecked())
        {
            int hour = QTime::currentTime().hour();
            if (hour == 0)
            {
                doSign();
            }
        }

        // 版权声明
        if (isLiving() && !settings->value("danmaku/copyright", false).toBool()
                && qrand() % 3 == 0)
        {
            /* if (shallAutoMsg() && (ui->autoSendWelcomeCheck->isChecked() || ui->autoSendGiftCheck->isChecked() || ui->autoSendAttentionCheck->isChecked()))
            {
                sendMsg(QString(QByteArray::fromBase64("562U6LCi5aes44CQ"))
                        +QApplication::applicationName()
                        +QByteArray::fromBase64("44CR5Li65oKo5pyN5Yqhfg=="));
            } */
        }
        triggerCmdEvent("NEW_HOUR", LiveDanmaku());

        // 判断每天最后一小时
        // 以 23:59:30为准
        QDateTime current = QDateTime::currentDateTime();
        QTime t = current.time();
        if (todayIsEnding) // 已经触发最后一小时事件了
        {
        }
        else if (current.time().hour() == 23
                || (t.hour() == 22 && t.minute() == 59 && t.second() > 30)) // 22:59:30之后的
        {
            todayIsEnding = true;
            QDateTime dt = current;
            QTime t = dt.time();
            t.setHMS(23, 59, 30); // 移动到最后半分钟
            dt.setTime(t);
            qint64 delta = dt.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
            if (delta < 0) // 可能已经是即将最后了
                delta = 0;
            QTimer::singleShot(delta, [=]{
                triggerCmdEvent("DAY_END", LiveDanmaku());

                // 判断每月最后一天
                QDate d = current.date();
                int days = d.daysInMonth();
                if (d.day() == days) // 1~31 == 28~31
                {
                    triggerCmdEvent("MONTH_END", LiveDanmaku());

                    // 判断每年最后一天
                    days = d.daysInYear();
                    if (d.dayOfYear() == days)
                    {
                        triggerCmdEvent("YEAR_END", LiveDanmaku());
                    }
                }

                // 判断每周最后一天
                if (d.dayOfWeek() == 7)
                {
                    triggerCmdEvent("WEEK_END", LiveDanmaku());
                }
            });
        }
    });
    hourTimer->start();

    // 每天的事件
    dayTimer = new QTimer(this);
    QTime zeroTime = QTime::currentTime();
    zeroTime.setHMS(0, 0, 1); // 本应当完全0点整的，避免误差
    QDate tomorrowDate = QDate::currentDate();
    tomorrowDate = tomorrowDate.addDays(1);
    QDateTime tomorrow(tomorrowDate, zeroTime);
    qint64 zeroSecond = tomorrow.toMSecsSinceEpoch();
    dayTimer->setInterval(zeroSecond - QDateTime::currentMSecsSinceEpoch());
    QDate currDate = QDate::currentDate();
    // 判断新的一天
    connect(dayTimer, &QTimer::timeout, this, [=]{
        todayIsEnding = false;
        dayTimer->setInterval(24*3600*1000);

        // 每天重新计算
        if (ui->calculateDailyDataCheck->isChecked())
            startCalculateDailyData();
        if (danmuLogFile && !isLiving())
            startSaveDanmakuToFile();
        userComeTimes.clear();
        sumPopul = 0;
        countPopul = 0;

        // 触发每天事件
        triggerCmdEvent("NEW_DAY", LiveDanmaku());
        triggerCmdEvent("NEW_DAY_FIRST", LiveDanmaku());
        settings->setValue("runtime/open_day", currDate.day());

        // 判断每一月初
        if (currDate.day() == 1)
        {
            triggerCmdEvent("NEW_MONTH", LiveDanmaku());
            triggerCmdEvent("NEW_MONTH_FIRST", LiveDanmaku());
            settings->setValue("runtime/open_month", currDate.month());

            // 判断每一年初
            if (currDate.month() == 1)
            {
                triggerCmdEvent("NEW_YEAR", LiveDanmaku());
                triggerCmdEvent("NEW_YEAR_FIRST", LiveDanmaku());
                triggerCmdEvent("HAPPY_NEW_YEAR", LiveDanmaku());
                settings->setValue("runtime/open_year", currDate.year());
            }
        }

        // 判断每周一
        if (currDate.dayOfWeek() == 1)
        {
            triggerCmdEvent("NEW_WEEK", LiveDanmaku());
            triggerCmdEvent("NEW_WEEK_FIRST", LiveDanmaku());
            settings->setValue("runtime/open_week_number", currDate.weekNumber());
        }
    });
    dayTimer->start();

    // 判断第一次打开
    int prevYear = settings->value("runtime/open_year", -1).toInt();
    int prevMonth = settings->value("runtime/open_month", -1).toInt();
    int prevDay = settings->value("runtime/open_day", -1).toInt();
    int prevWeekNumber = settings->value("runtime/open_week_number", -1).toInt();
    if (prevYear != currDate.year())
    {
        prevMonth = prevDay = -1; // 避免是不同年的同一月
        triggerCmdEvent("NEW_YEAR_FIRST", LiveDanmaku());
        settings->setValue("runtime/open_year", currDate.year());
    }
    if (prevMonth != currDate.month())
    {
        prevDay = -1; // 避免不同月的同一天
        triggerCmdEvent("NEW_MONTH_FIRST", LiveDanmaku());
        settings->setValue("runtime/open_month", currDate.month());
    }
    if (prevDay != currDate.day())
    {
        triggerCmdEvent("NEW_DAY_FIRST", LiveDanmaku());
        settings->setValue("runtime/open_day", currDate.day());
    }
    if (prevWeekNumber != currDate.weekNumber())
    {
        triggerCmdEvent("NEW_WEEK_FIRST", LiveDanmaku());
        settings->setValue("runtime/open_week_number", currDate.weekNumber());
    }

    // 调试模式
    localDebug = settings->value("debug/localDebug", false).toBool();
    ui->actionLocal_Mode->setChecked(localDebug);
    debugPrint = settings->value("debug/debugPrint", false).toBool();
    ui->actionDebug_Mode->setChecked(debugPrint);
    saveRecvCmds = settings->value("debug/saveRecvCmds", false).toBool();
    ui->saveRecvCmdsCheck->setChecked(saveRecvCmds);
    if (saveRecvCmds)
        on_saveRecvCmdsCheck_clicked();

    // 模拟CMDS
    ui->timerPushCmdCheck->setChecked(settings->value("debug/pushCmdsTimer", false).toBool());
    ui->timerPushCmdSpin->setValue(settings->value("debug/pushCmdsInterval", 10).toInt());

    if (!settings->value("danmaku/copyright", false).toBool())
    {
        /* if (shallAutoMsg() && (ui->autoSendWelcomeCheck->isChecked() || ui->autoSendGiftCheck->isChecked() || ui->autoSendAttentionCheck->isChecked()))
        {
            localNotify(QString(QByteArray::fromBase64("44CQ"))
                        +QApplication::applicationName()
                        +QByteArray::fromBase64("44CR5Li65oKo5pyN5Yqhfg=="));
        } */
    }

    // 开启服务端
    bool enableServer = settings->value("server/enabled", false).toBool();
    ui->serverCheck->setChecked(enableServer);
    int port = settings->value("server/port", 5520).toInt();
    ui->serverPortSpin->setValue(port);
    serverDomain = settings->value("server/domain", "localhost").toString();
    ui->allowWebControlCheck->setChecked(settings->value("server/allowWebControl", false).toBool());
    ui->allowRemoteControlCheck->setChecked(remoteControl = settings->value("danmaku/remoteControl", true).toBool());
    ui->allowAdminControlCheck->setChecked(settings->value("danmaku/adminControl", false).toBool());
    ui->domainEdit->setText(serverDomain);
    if (enableServer)
    {
        openServer();
    }

    // 设置默认配置
    if (firstOpen)
    {
        readDefaultCode();
    }

    // 恢复游戏数据
    restoreGameNumbers();
    restoreGameTexts();

    triggerCmdEvent("START_UP", LiveDanmaku());
}

void BaseWindow::initPath()
{
    dataPath = QApplication::applicationDirPath() + "/";
#ifdef Q_OS_WIN
    // 如果没有设置通用目录，则选择安装文件夹
    if (QFileInfo(dataPath+"green_version").exists()
            || QFileInfo(dataPath+"green_version.txt").exists())
    {
        // 安装路径，不需要改

    }
    else // 通用文件夹
    {
        dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        // C:/Users/Administrator/AppData/Roaming/神奇弹幕    (未定义ApplicationName时为exe名)
        SOCKET_DEB << "路径：" << dataPath;
    }
#else
    dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/";
    QDir().mkPath(dataPath);
#endif
}

void BaseWindow::readConfig()
{

}

BaseWindow::~BaseWindow()
{
    if (danmuLogFile)
    {
        finishSaveDanmuToFile();
    }
    if (ui->calculateDailyDataCheck->isChecked())
    {
        saveCalculateDailyData();
    }

    if (danmakuWindow)
    {
        danmakuWindow->close();
        danmakuWindow->deleteLater();
        danmakuWindow = nullptr;
    }

    /*if (playerWindow)
    {
        settings->setValue("danmaku/playerWindow", !playerWindow->isHidden());
        playerWindow->close();
        playerWindow->deleteLater();
    }*/

    triggerCmdEvent("SHUT_DOWN", LiveDanmaku());

    delete ui;

    if (isFileExist(webCache("")))
        deleteDir(webCache(""));
}

const QSettings* BaseWindow::getSettings() const
{
    return settings;
}

void BaseWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    restoreGeometry(settings->value("mainwindow/geometry").toByteArray());

    static bool firstShow = true;
    if (firstShow)
    {
        firstShow = false;
        startSplash();
    }
    settings->setValue("mainwindow/autoShow", true);
}

void BaseWindow::closeEvent(QCloseEvent *event)
{
    settings->setValue("mainwindow/geometry", this->saveGeometry());

#if defined(ENABLE_TRAY)
    event->ignore();
    this->hide();

    QTimer::singleShot(5000, [=]{
        if (!this->isHidden())
            return ;
        settings->setValue("mainwindow/autoShow", false);
    });
#else
    QMainWindow::closeEvent(event);
#endif
}

void BaseWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // 自动调整封面大小
    if (!roomCover.isNull())
    {
        QPixmap pixmap = roomCover;
        int w = ui->roomCoverLabel->width();
        if (w > ui->tabWidget->contentsRect().width())
            w = ui->tabWidget->contentsRect().width();
        pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
        ui->roomCoverLabel->setPixmap(getRoundedPixmap(pixmap));
        ui->roomCoverLabel->setMinimumSize(1, 1);
    }

    // 自动调整任务列表大小
    for (int row = 0; row < ui->taskListWidget->count(); row++)
    {
        auto item = ui->taskListWidget->item(row);
        auto widget = ui->taskListWidget->itemWidget(item);
        if (!widget)
            continue;
        QSize size(ui->taskListWidget->contentsRect().width() - ui->taskListWidget->verticalScrollBar()->width(), widget->height());
        auto taskWidget = static_cast<TaskWidget*>(widget);
        taskWidget->resize(size);
        taskWidget->autoResizeEdit();
    }

    for (int row = 0; row < ui->replyListWidget->count(); row++)
    {
        auto item = ui->replyListWidget->item(row);
        auto widget = ui->replyListWidget->itemWidget(item);
        if (!widget)
            continue;
        QSize size(ui->replyListWidget->contentsRect().width() - ui->replyListWidget->verticalScrollBar()->width(), widget->height());
        auto replyWidget = static_cast<ReplyWidget*>(widget);
        replyWidget->resize(size);
        replyWidget->autoResizeEdit();
    }

    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto item = ui->eventListWidget->item(row);
        auto widget = ui->eventListWidget->itemWidget(item);
        if (!widget)
            continue;
        QSize size(ui->eventListWidget->contentsRect().width() - ui->eventListWidget->verticalScrollBar()->width(), widget->height());
        auto eventWidget = static_cast<EventWidget*>(widget);
        eventWidget->resize(size);
        eventWidget->autoResizeEdit();
    }
}

void BaseWindow::changeEvent(QEvent *event)
{

    QMainWindow::changeEvent(event);
}

void BaseWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QPainter painter(this);
}

void BaseWindow::pullLiveDanmaku()
{
    if (roomId.isEmpty())
        return ;
    QString url = "https://api.live.bilibili.com/ajax/msg";
    QStringList param{"roomid", roomId};
    connect(new NetUtil(url, param), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "pullLiveDanmaku.ERROR:" << error.errorString();
            qCritical() << result;
            return ;
        }
        QJsonObject json = document.object();
        QJsonArray danmakus = json.value("data").toObject().value("room").toArray();
        QDateTime time = QDateTime::currentDateTime();
        qint64 removeTime = time.toMSecsSinceEpoch() - removeDanmakuInterval;
        for (int i = 0; i < danmakus.size(); i++)
        {
            LiveDanmaku danmaku = LiveDanmaku::fromDanmakuJson(danmakus.at(i).toObject());
            if (danmaku.getTimeline().toMSecsSinceEpoch() < removeTime)
                continue;
            danmaku.transToDanmu();
            danmaku.setTime(time);
            danmaku.setNoReply();
            appendNewLiveDanmaku(danmaku);
        }
    });
}

void BaseWindow::removeTimeoutDanmaku()
{
    if (pushCmdsFile && roomDanmakus.size() < 1000) // 不移除弹幕
        return ;

    // 移除过期队列
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    qint64 removeTime = timestamp - removeDanmakuInterval;
    if (roomDanmakus.size())
    {
        QDateTime dateTime = roomDanmakus.first().getTimeline();
        if (dateTime.toMSecsSinceEpoch() < removeTime)
        {
            auto danmaku = roomDanmakus.takeFirst();
            oldLiveDanmakuRemoved(danmaku);
        }
    }

    // 移除多余的提示（一般时间更短）
    removeTime = timestamp - removeDanmakuTipInterval;
    for (int i = 0; i < roomDanmakus.size(); i++)
    {
        auto danmaku = roomDanmakus.at(i);
        auto type = danmaku.getMsgType();
        if (type == MSG_ATTENTION || type == MSG_WELCOME || type == MSG_FANS
                || (type == MSG_GIFT && (!danmaku.isGoldCoin() || danmaku.getTotalCoin() < 1000))
                || (type == MSG_DANMAKU && danmaku.isNoReply())
                || type == MSG_MSG
                || danmaku.isToView() || danmaku.isPkLink())
        {
            QDateTime dateTime = danmaku.getTimeline();
            if (dateTime.toMSecsSinceEpoch() < removeTime)
            {
                roomDanmakus.removeAt(i--);
                oldLiveDanmakuRemoved(danmaku);
            }
            // break; // 不break，就是一次性删除多个
        }
    }
}

void BaseWindow::appendNewLiveDanmakus(QList<LiveDanmaku> danmakus)
{
    // 添加到队列
    roomDanmakus.append(danmakus);
    allDanmakus.append(danmakus);
}

void BaseWindow::appendNewLiveDanmaku(LiveDanmaku danmaku)
{
    roomDanmakus.append(danmaku);
    lastDanmaku = danmaku;
    allDanmakus.append(danmaku);
    newLiveDanmakuAdded(danmaku);
}

void BaseWindow::newLiveDanmakuAdded(LiveDanmaku danmaku)
{
    SOCKET_DEB << "+++++新弹幕：" << danmaku.toString();
    emit signalNewDanmaku(danmaku);

    // 保存到文件
    if (danmuLogStream)
    {
        if (danmaku.is(MSG_DEF) && danmaku.getText().startsWith("["))
            return ;
        (*danmuLogStream) << danmaku.toString() << "\n";
        (*danmuLogStream).flush(); // 立刻刷新到文件里
    }
}

void BaseWindow::oldLiveDanmakuRemoved(LiveDanmaku danmaku)
{
    SOCKET_DEB << "-----旧弹幕：" << danmaku.toString();
    emit signalRemoveDanmaku(danmaku);
}

void BaseWindow::addNoReplyDanmakuText(QString text)
{
    noReplyMsgs.append(text);
}

bool BaseWindow::isLiving() const
{
    return liveStatus == 1;
}

void BaseWindow::localNotify(QString text)
{
	if (text.isEmpty())
    	return ;
    appendNewLiveDanmaku(LiveDanmaku(text));
}

void BaseWindow::localNotify(QString text, qint64 uid)
{
    LiveDanmaku danmaku(text);
    danmaku.setUid(uid);
    appendNewLiveDanmaku(danmaku);
}

/**
 * 发送单条弹幕的原子操作
 */
void BaseWindow::sendMsg(QString msg)
{
    sendRoomMsg(roomId, msg);
}

void BaseWindow::sendRoomMsg(QString roomId, QString msg)
{
    if (browserCookie.isEmpty() || browserData.isEmpty())
    {
        statusLabel->setText("未设置Cookie信息");
        return ;
    }
    if (msg.isEmpty() || roomId.isEmpty())
        return ;

    if (localDebug)
    {
        localNotify("发送弹幕 -> " + msg + "  (" + snum(msg.length()) + ")");
        return ;
    }

    // 设置数据（JSON的ByteArray）
    QString s = browserData;
    int posl = s.indexOf("msg=")+4;
    int posr = s.indexOf("&", posl);
    if (posr == -1)
        posr = s.length();
    s.replace(posl, posr-posl, msg);

    posl = s.indexOf("roomid=")+7;
    posr = s.indexOf("&", posl);
    if (posr == -1)
        posr = s.length();
    s.replace(posl, posr-posl, roomId);

    QByteArray ba(s.toStdString().data());

    // 连接槽
    post("https://api.live.bilibili.com/msg/send", ba, [=](QJsonObject json){
        QString errorMsg = json.value("message").toString();
        statusLabel->setText("");
        if (!errorMsg.isEmpty())
        {
            statusLabel->setText(errorMsg);
            qCritical() << s8("warning: 发送失败：") << errorMsg << msg;
            localNotify(errorMsg + " -> " + msg);

            if (!ui->retryFailedDanmuCheck->isChecked())
                return ;

            if (roomId != this->roomId)
                return ;
            if (errorMsg.contains("msg in 1s"))
            {
                localNotify("[5s后重试]");
                sendAutoMsgInFirst(msg, 5000);
                /* QTimer::singleShot(5000, [=]{ // 太快的话会repeat
                    if (room != this->roomId) // 换房间了
                        return ;
                    sendAutoMsg(msg);
                }); */
            }
            else if (errorMsg.contains("msg repeat"))
            {
                localNotify("[3s后重试]");
                sendAutoMsgInFirst(msg, 3200);
            }
            else if (errorMsg.contains("超出限制长度"))
            {
                if (msg.length() <= ui->danmuLongestSpin->value())
                {
                    localNotify("[错误的弹幕长度：超出长度" + snum(msg.length()) + " <= 设置长度" + snum(ui->danmuLongestSpin->value()) + "]");
                }
                else
                {
                    localNotify("[自动分割长度]");
                    sendAutoMsgInFirst(splitLongDanmu(msg).join("\\n"), 1000);
                }
            }
            else if (errorMsg == "f") // 系统敏感词
            {
            }
            else if (errorMsg == "k") // 主播设置的直播间敏感词
            {
            }
        }
    });
}

/**
 * 发送多条消息
 * 使用“\n”进行多行换行
 */
void BaseWindow::sendAutoMsg(QString msgs)
{
    if (msgs.trimmed().isEmpty())
    {
        if (debugPrint)
            localNotify("[空弹幕，已忽略]");
        return ;
    }
    QStringList sl = msgs.split("\\n", QString::SkipEmptyParts);
    autoMsgQueues.append(sl);
    if (!autoMsgTimer->isActive() || !inDanmakuCd)
    {
        slotSendAutoMsg(false); // 先运行一次
        autoMsgTimer->start();
    }
}

void BaseWindow::sendAutoMsgInFirst(QString msgs, int interval)
{
    if (msgs.trimmed().isEmpty())
    {
        if (debugPrint)
            localNotify("[空弹幕，已忽略]");
        return ;
    }
    QStringList sl = msgs.split("\\n", QString::SkipEmptyParts);
    autoMsgQueues.insert(0, sl);
    if (interval > 0)
        autoMsgTimer->setInterval(interval);
    if (!autoMsgTimer->isActive())
        autoMsgTimer->start();
}

/**
 * 执行发送队列中的发送弹幕，或者函数操作
 * // @return 是否是执行命令。为空或发送弹幕为false
 */
void BaseWindow::slotSendAutoMsg(bool timeout)
{
    if (timeout) // 全部发完之后 timer 一定还开着的，最后一次 timeout 清除弹幕发送冷却
        inDanmakuCd = false;

    if (autoMsgQueues.isEmpty())
    {
        autoMsgTimer->stop();
        return ;
    }

    if (autoMsgTimer->interval() != AUTO_MSG_CD) // 之前命令修改过延时
        autoMsgTimer->setInterval(AUTO_MSG_CD);

    QStringList& sl = autoMsgQueues[0];
    QString msg = sl.takeFirst();
    if (sl.isEmpty())
        autoMsgQueues.removeFirst();
    if (!autoMsgQueues.size())
        autoMsgTimer->stop();

    CmdResponse res = NullRes;
    int resVal = 0;
    if (!execFunc(msg, res, resVal)) // 先判断能否执行命令，如果是发送弹幕
    {
        msg = msgToShort(msg);
        addNoReplyDanmakuText(msg);
        sendMsg(msg);
        inDanmakuCd = true;
    }
    else // 是执行命令，发送下一条弹幕就不需要延迟了
    {
        if (res == AbortRes) // 终止这一轮后面的弹幕
        {
            if (!sl.isEmpty()) // 如果为空，则自动为终止
                autoMsgQueues.removeFirst();
            return ;
        }
        else if (res == DelayRes) // 修改延迟
        {
            if (resVal < 0)
                qCritical() << "设置延时时间出错";
            autoMsgTimer->setInterval(resVal);
            return ;
        }
    }

    // 如果后面是命令的话，尝试立刻执行
    if (autoMsgQueues.size())
    {
        QString nextMsg = autoMsgQueues.first().first();
        QRegularExpression re("^\\s*>");
        if (nextMsg.indexOf(re) > -1) // 下一条是命令，直接执行
        {
            slotSendAutoMsg(false); // 递归
        }
    }
}

/**
 * 发送前确保没有需要调整的变量了
 */
void BaseWindow::sendCdMsg(QString msg, int cd, int channel, bool enableText, bool enableVoice, bool manual)
{
    if (!manual && !shallAutoMsg()) // 不在直播中
    {
        qDebug() << "未开播，不做操作(cd)" << msg;
        if (debugPrint)
            localNotify("[未开播，不做操作]");
        return ;
    }
    if (msg.trimmed().isEmpty())
    {
        if (debugPrint)
            localNotify("[空弹幕，已跳过]");
        return ;
    }

    analyzeMsgAndCd(msg, cd, channel);

    // 避免太频繁发消息
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    if (timestamp - msgCds[channel] < cd)
    {
        if (debugPrint)
            localNotify("[未完成冷却：" + snum(timestamp - msgCds[channel]) + "," + snum(cd) + "ms]");
        return ;
    }
    msgCds[channel] = timestamp;

    if (enableText)
    {
//        if (debugPrint)
//            localNotify("[发送弹幕：" + msg + "]");
        sendAutoMsg(msg);
    }
    if (enableVoice)
        speekVariantText(msg);
}

void BaseWindow::sendGiftMsg(QString msg)
{
    sendCdMsg(msg, ui->sendGiftCDSpin->value() * 1000, GIFT_CD_CN,
              ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked());
}

void BaseWindow::sendAttentionMsg(QString msg)
{
    sendCdMsg(msg, ui->sendAttentionCDSpin->value() * 1000, GIFT_CD_CN,
              ui->sendAttentionTextCheck->isChecked(), ui->sendAttentionVoiceCheck->isChecked());
}

void BaseWindow::sendNotifyMsg(QString msg, bool manual)
{
    sendCdMsg(msg, NOTIFY_CD, NOTIFY_CD_CN,
              true, false, manual);
}

/**
 * 连击定时器到达
 */
void BaseWindow::slotComboSend()
{
    if (!giftCombos.size())
    {
        comboTimer->stop();
        return ;
    }

    qint64 timestamp = QDateTime::currentSecsSinceEpoch();
    int delta = ui->giftComboDelaySpin->value();

    auto thankGift = [=](LiveDanmaku danmaku) -> bool {
        QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
        if (words.size())
        {
            int r = qrand() % words.size();
            QString msg = words.at(r);
            if (strongNotifyUsers.contains(danmaku.getUid()))
            {
                if (debugPrint)
                    localNotify("[强提醒]");
                sendCdMsg(msg, NOTIFY_CD, GIFT_CD_CN,
                          ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked());
            }
            else
            {
                sendGiftMsg(msg);
            }
            return true;
        }
        else if (debugPrint)
        {
            localNotify("[没有可发送的连击答谢弹幕]");
            return false;
        }
        return false;
    };

    if (!ui->giftComboTopCheck->isChecked()) // 全部答谢，看冷却时间
    {
        for (auto it = giftCombos.begin(); it != giftCombos.end(); )
        {
            const LiveDanmaku& danmaku = it.value();
            if (danmaku.getTimeline().toSecsSinceEpoch() + delta > timestamp) // 未到达统计时间
            {
                it++;
                continue;
            }

            // 开始答谢该项
            thankGift(danmaku);
            it = giftCombos.erase(it);
        }
    }
    else // 等待结束，只答谢最贵的一项
    {
        qint64 maxGold = 0;
        auto maxIt = giftCombos.begin();
        for (auto it = giftCombos.begin(); it != giftCombos.end(); it++)
        {
            const LiveDanmaku& danmaku = it.value();
            if (danmaku.getTimeline().toSecsSinceEpoch() + delta > timestamp) // 还有礼物未到答谢时间
                return ;
            if (danmaku.isGoldCoin() && danmaku.getTotalCoin() > maxGold)
            {
                maxGold = danmaku.getTotalCoin();
                maxIt = it;
            }
        }
        if (maxGold > 0)
        {
            thankGift(maxIt.value());
            giftCombos.clear();
            return ;
        }

        // 没有金瓜子，只有银瓜子
        int maxSilver = 0;
        maxIt = giftCombos.begin();
        for (auto it = giftCombos.begin(); it != giftCombos.end(); it++)
        {
            const LiveDanmaku& danmaku = it.value();
            if (danmaku.getTotalCoin() > maxGold)
            {
                maxSilver = danmaku.getTotalCoin();
                maxIt = it;
            }
        }
        thankGift(maxIt.value());
        giftCombos.clear();
        return ;
    }

}

void BaseWindow::on_DiangeAutoCopyCheck_stateChanged(int)
{
    settings->setValue("danmaku/diangeAutoCopy", diangeAutoCopy = ui->DiangeAutoCopyCheck->isChecked());
}

void BaseWindow::on_testDanmakuButton_clicked()
{
    QString text = ui->testDanmakuEdit->text();
    if (text.isEmpty())
        text = "测试弹幕";
    qint64 uid = 123;
    int r = qrand() % 7 + 1;
    if (text.startsWith("$"))
    {
        text.replace(0, 1, "");
        uid = cookieUid.toLongLong();
    }
    else
    {
        uid = 10000 + r;
    }

    if (text == "赠送吃瓜")
    {
        sendGift(20004, 1);
    }
    else if (text == "测试送礼")
    {
        QString username = "测试用户";
        QString giftName = "测试礼物";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        appendNewLiveDanmaku(danmaku);
        if (ui->saveEveryGiftCheck->isChecked())
            saveEveryGift(danmaku);

        QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
        qDebug() << "条件替换结果：" << words;

        triggerCmdEvent("SEND_GIFT", danmaku);
    }
    else if (text == "测试礼物连击")
    {
        QString username = "测试用户";
        QString giftName = "测试礼物";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        uid = 123456;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);

        bool merged = mergeGiftCombo(danmaku); // 如果有合并，则合并到之前的弹幕上面
        if (!merged)
        {
            appendNewLiveDanmaku(danmaku);
        }
    }
    else if (text == "测试醒目留言")
    {
        LiveDanmaku danmaku("测试用户", "测试弹幕", 1001, 12, QDateTime::currentDateTime(), "", "", 39, "醒目留言", 1, 30);
        appendNewLiveDanmaku(danmaku);
    }
    else if (text == "测试消息")
    {
        localNotify("测试通知消息");
    }
    else if (text == "测试舰长进入")
    {
        QString uname = "测试舰长";
        LiveDanmaku danmaku(qrand() % 3 + 1, uname, uid, QDateTime::currentDateTime());
        appendNewLiveDanmaku(danmaku);

        if (ui->autoSendWelcomeCheck->isChecked() && !notWelcomeUsers.contains(uid))
        {
            sendWelcome(danmaku);
        }
    }
    else if (text == "测试舰长")
    {
        QString username = "测试用户";
        QString giftName = "舰长";
        int num = qrand() % 3 + 1;
        int gift_id = 10003;
        int guard_level = 3;
        int price = 198000;
        int rg = qrand() % 6;
        if (rg < 3)
        {
            giftName = "舰长";
            guard_level = 3;
            price = 198000;
        }
        else if (rg < 5)
        {
            giftName = "提督";
            guard_level = 2;
            price = 1980000;
        }
        else
        {
            giftName = "总督";
            guard_level = 1;
            price = 19800000;
        }
        gift_id = 10000 + guard_level;
        LiveDanmaku danmaku(username, uid, giftName, num, guard_level, gift_id, price, 2);
        appendNewLiveDanmaku(danmaku);
        appendLiveGuard(danmaku);
        if (ui->saveEveryGuardCheck->isChecked())
            saveEveryGuard(danmaku);

        if (!justStart && ui->autoSendGiftCheck->isChecked())
        {
            QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
            if (words.size())
            {
                int r = qrand() % words.size();
                QString msg = words.at(r);
                sendCdMsg(msg, NOTIFY_CD, NOTIFY_CD_CN,
                          ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked());
            }
            else if (debugPrint)
            {
                localNotify("[没有可发送的上船弹幕]");
            }
        }
    }
    else if (text.startsWith(">"))
    {
        processRemoteCmd(text.replace(0, 1, ""));
    }
    else if (text == "测试开播")
    {
        QString text = ui->startLiveWordsEdit->text();
        if (ui->startLiveSendCheck->isChecked() && !text.trimmed().isEmpty())
            sendAutoMsg(text);
        ui->popularityLabel->setText("已开播");
        liveStatus = 1;
        if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
            connectServerTimer->stop();
        slotStartWork(); // 每个房间第一次开始工作

        triggerCmdEvent("LIVE", LiveDanmaku());
    }
    else if (text == "测试对面主播进入")
    {
        QJsonObject json;
        json.insert("cmd", "INTERACT_WORD");
        QJsonObject data;
        data.insert("msg_type", 1);
        data.insert("timestamp", QDateTime::currentSecsSinceEpoch());
        data.insert("uid", pkUid.toLongLong());
        data.insert("uname", "测试用户");
        json.insert("data", data);
        handleMessage(json);
        qDebug() << pkUid << oppositeAudience;
    }
    else if (text == "测试花灯")
    {
        QString username = "测试用户";
        QString giftName = "炫彩花灯";
        int r = qrand() % 5;
        if (r == 1)
            giftName = "漫天花灯";
        else if (r == 2)
            giftName = "暴富花灯";
        int giftId = 123;
        int num = qrand() % 3 + 1;
        qint64 timestamp = QDateTime::currentSecsSinceEpoch();
        QString coinType = qrand()%2 ? "gold" : "silver";
        int totalCoin = qrand() % 20 * 100;
        uid = 123456;
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        appendNewLiveDanmaku(danmaku);
        triggerCmdEvent("SEND_GIFT", danmaku);
    }
    else if (text == "测试高能榜")
    {
        QByteArray ba = "{ \"cmd\": \"ENTRY_EFFECT\", \"data\": { \"basemap_url\": \"https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png\", \"business\": 3, \"copy_color\": \"#000000\", \"copy_writing\": \"欢迎 <%___君陌%> 进入直播间\", \"copy_writing_v2\": \"欢迎 <^icon^> <%___君陌%> 进入直播间\", \"effective_time\": 2, \"face\": \"https://i1.hdslb.com/bfs/face/8fb8336e1ae50001ca76b80c30b01d23b07203c9.jpg\", \"highlight_color\": \"#FFF100\", \"icon_list\": [ 2 ], \"id\": 136, \"max_delay_time\": 7, \"mock_effect\": 0, \"priority\": 1, \"privilege_type\": 0, \"show_avatar\": 1, \"target_id\": 5988102, \"uid\": 453364, \"web_basemap_url\": \"https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png\", \"web_close_time\": 900, \"web_effect_close\": 0, \"web_effective_time\": 2 } }";
        handleMessage(QJsonDocument::fromJson(ba).object());
    }
    else if (text == "测试对面信息")
    {
        getPkMatchInfo();
    }
    else if (text == "测试对面舰长")
    {
        getPkOnlineGuardPage(0);
    }
    else
    {
        appendNewLiveDanmaku(LiveDanmaku("测试用户" + QString::number(r), text,
                             uid, 12,
                             QDateTime::currentDateTime(), "", ""));
    }
}

void BaseWindow::on_removeDanmakuIntervalSpin_valueChanged(int arg1)
{
    this->removeDanmakuInterval = arg1 * 1000;
    settings->setValue("danmaku/removeInterval", arg1);
}

void BaseWindow::on_roomIdEdit_editingFinished()
{
    if (roomId == ui->roomIdEdit->text() || shortId == ui->roomIdEdit->text())
        return ;

    // 关闭旧的
    if (socket)
    {
        liveStatus = 0;
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->abort();
    }
    roomId = ui->roomIdEdit->text();
    upUid = "";
    settings->setValue("danmaku/roomId", roomId);

    releaseLiveData();

    emit signalRoomChanged(roomId);

    // 开启新的
    if (socket)
    {
        startConnectRoom();
    }
}

void BaseWindow::on_languageAutoTranslateCheck_stateChanged(int)
{
    auto trans = ui->languageAutoTranslateCheck->isChecked();
    settings->setValue("danmaku/autoTrans", trans);
    if (danmakuWindow)
        danmakuWindow->setAutoTranslate(trans);
}

void BaseWindow::on_tabWidget_tabBarClicked(int index)
{
    settings->setValue("mainwindow/tabIndex", index);
}

void BaseWindow::on_SendMsgButton_clicked()
{
    QString msg = ui->SendMsgEdit->text();
    msg = processDanmakuVariants(msg, LiveDanmaku());
    sendAutoMsg(msg);
}

void BaseWindow::on_AIReplyCheck_stateChanged(int)
{
    bool reply = ui->AIReplyCheck->isChecked();
    settings->setValue("danmaku/aiReply", reply);
    if (danmakuWindow)
        danmakuWindow->setAIReply(reply);

    ui->AIReplyMsgCheck->setEnabled(reply);
}

void BaseWindow::on_testDanmakuEdit_returnPressed()
{
    on_testDanmakuButton_clicked();

    ui->testDanmakuEdit->setText("");
}

void BaseWindow::on_SendMsgEdit_returnPressed()
{
    QString msg = ui->SendMsgEdit->text();
    msg = processDanmakuVariants(msg, LiveDanmaku());
    sendAutoMsg(msg);
    ui->SendMsgEdit->clear();
}

TaskWidget* BaseWindow::addTimerTask(bool enable, int second, QString text, int index)
{
    TaskWidget* tw = new TaskWidget(this);
    QListWidgetItem* item;

    if (index == -1)
    {
        item = new QListWidgetItem(ui->taskListWidget);
        ui->taskListWidget->addItem(item);
        ui->taskListWidget->setItemWidget(item, tw);
        // ui->taskListWidget->setCurrentRow(ui->taskListWidget->count()-1);
    }
    else
    {
        item = new QListWidgetItem();
        ui->taskListWidget->insertItem(index, item);
        ui->taskListWidget->setItemWidget(item, tw);
        ui->taskListWidget->setCurrentRow(index);
    }

    // 连接信号
    connect(tw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = tw->check->isChecked();
        int row = ui->taskListWidget->row(item);
        settings->setValue("task/r"+QString::number(row)+"Enable", enable);
    });

    connect(tw, &TaskWidget::spinChanged, this, [=](int val){
        int row = ui->taskListWidget->row(item);
        settings->setValue("task/r"+QString::number(row)+"Interval", val);
    });

    connect(tw->edit, &ConditionEditor::textChanged, this, [=]{
        item->setSizeHint(tw->sizeHint());

        QString content = tw->edit->toPlainText();
        int row = ui->taskListWidget->row(item);
        settings->setValue("task/r"+QString::number(row)+"Msg", content);
    });

    connect(tw, &TaskWidget::signalSendMsgs, this, [=](QString sl, bool manual){
        if (!manual && !shallAutoMsg(sl, manual)) // 没有开播，不进行定时任务
        {
            qDebug() << "未开播，不做回复(timer)" << sl;
            if (debugPrint)
                localNotify("[未开播，不做回复]");
            return ;
        }
        QStringList msgs = getEditConditionStringList(sl, LiveDanmaku());
        if (msgs.size())
        {
            int r = qrand() % msgs.size();
            QString s = msgs.at(r);
            if (!s.trimmed().isEmpty())
            {
                sendAutoMsg(s);
            }
        }
    });

    connect(tw, &TaskWidget::signalResized, tw, [=]{
        item->setSizeHint(tw->size());
    });

    remoteControl = settings->value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    tw->check->setChecked(enable);
    tw->spin->setValue(second);
    tw->slotSpinChanged(second);
    tw->edit->setPlainText(text);
    tw->autoResizeEdit();
    tw->adjustSize();
//    item->setSizeHint(tw->sizeHint());

    return tw;
}

TaskWidget *BaseWindow::addTimerTask(MyJson json)
{
    auto item = addTimerTask(false, 1800, "");
    item->fromJson(json);
    return item;
}

void BaseWindow::saveTaskList()
{
    settings->setValue("task/count", ui->taskListWidget->count());
    for (int row = 0; row < ui->taskListWidget->count(); row++)
    {
        auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(row));
        auto tw = static_cast<TaskWidget*>(widget);
        settings->setValue("task/r"+QString::number(row)+"Enable", tw->check->isChecked());
        settings->setValue("task/r"+QString::number(row)+"Interval", tw->spin->value());
        settings->setValue("task/r"+QString::number(row)+"Msg", tw->edit->toPlainText());
    }
}

void BaseWindow::restoreTaskList()
{
    int count = settings->value("task/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = settings->value("task/r"+QString::number(row)+"Enable", false).toBool();
        int interval = settings->value("task/r"+QString::number(row)+"Interval", 1800).toInt();
        QString msg = settings->value("task/r"+QString::number(row)+"Msg", "").toString();
        addTimerTask(enable, interval, msg);
    }
}

ReplyWidget* BaseWindow::addAutoReply(bool enable, QString key, QString reply, int index)
{
    ReplyWidget* rw = new ReplyWidget(this);
    QListWidgetItem* item;

    if (index == -1)
    {
        item = new QListWidgetItem(ui->replyListWidget);
        ui->replyListWidget->addItem(item);
        ui->replyListWidget->setItemWidget(item, rw);
        // ui->replyListWidget->setCurrentRow(ui->replyListWidget->count()-1);
    }
    else
    {
        item = new QListWidgetItem();
        ui->replyListWidget->insertItem(index, item);
        ui->replyListWidget->setItemWidget(item, rw);
        ui->replyListWidget->setCurrentRow(index);
    }

    // 连接信号
    connect(rw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = rw->check->isChecked();
        int row = ui->replyListWidget->row(item);
        settings->setValue("reply/r"+QString::number(row)+"Enable", enable);
    });

    connect(rw->keyEdit, &QLineEdit::textChanged, this, [=](const QString& text){
        int row = ui->replyListWidget->row(item);
        settings->setValue("reply/r"+QString::number(row)+"Key", text);
    });

    connect(rw->replyEdit, &ConditionEditor::textChanged, this, [=]{
        item->setSizeHint(rw->sizeHint());

        QString content = rw->replyEdit->toPlainText();
        int row = ui->replyListWidget->row(item);
        settings->setValue("reply/r"+QString::number(row)+"Reply", content);
    });

    connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), rw, SLOT(slotNewDanmaku(LiveDanmaku)));

    connect(rw, &ReplyWidget::signalReplyMsgs, this, [=](QString sl, LiveDanmaku danmaku, bool manual){
        if ((!manual && !shallAutoMsg(sl, manual)) || danmaku.isPkLink()) // 没有开播，不进行自动回复
        {
            if (!danmaku.isPkLink())
                qDebug() << "未开播，不做回复(reply)" << sl;
            if (debugPrint)
                localNotify("[未开播，不做回复]");
            return ;
        }
        QStringList msgs = getEditConditionStringList(sl, danmaku);
        if (msgs.size())
        {
            int r = qrand() % msgs.size();
            QString s = msgs.at(r);
            if (!s.trimmed().isEmpty())
            {
                if (QString::number(danmaku.getUid()) == this->cookieUid) // 自己发的，自己回复，必须要延迟一会儿
                {
                    if (s.contains(QRegExp("\\(\\s*cd\\d+\\s*:\\s*\\d+\\s*\\)"))) // 带冷却通道，不能放前面
                        autoMsgTimer->start(); // 先启动，避免立即发送
                    else
                        s = "\\n" + s; // 延迟一次发送的时间
                }
                sendCdMsg(s, 0, REPLY_CD_CN, true, false, manual);
            }
        }
    });

    connect(rw, &ReplyWidget::signalResized, rw, [=]{
        item->setSizeHint(rw->size());
    });

    remoteControl = settings->value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    rw->check->setChecked(enable);
    rw->keyEdit->setText(key);
    rw->replyEdit->setPlainText(reply);
    rw->autoResizeEdit();
    rw->adjustSize();
    item->setSizeHint(rw->sizeHint());

    return rw;
}

ReplyWidget *BaseWindow::addAutoReply(MyJson json)
{
    auto item = addAutoReply(false, "", "");
    item->fromJson(json);
    return item;
}

void BaseWindow::saveReplyList()
{
    settings->setValue("reply/count", ui->replyListWidget->count());
    for (int row = 0; row < ui->replyListWidget->count(); row++)
    {
        auto widget = ui->replyListWidget->itemWidget(ui->replyListWidget->item(row));
        auto tw = static_cast<ReplyWidget*>(widget);
        settings->setValue("reply/r"+QString::number(row)+"Enable", tw->check->isChecked());
        settings->setValue("reply/r"+QString::number(row)+"Key", tw->keyEdit->text());
        settings->setValue("reply/r"+QString::number(row)+"Reply", tw->replyEdit->toPlainText());
    }
}

void BaseWindow::restoreReplyList()
{
    int count = settings->value("reply/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = settings->value("reply/r"+QString::number(row)+"Enable", false).toBool();
        QString key = settings->value("reply/r"+QString::number(row)+"Key").toString();
        QString reply = settings->value("reply/r"+QString::number(row)+"Reply").toString();
        addAutoReply(enable, key, reply);
    }
}

EventWidget* BaseWindow::addEventAction(bool enable, QString cmd, QString action, int index)
{
    EventWidget* rw = new EventWidget(this);
    QListWidgetItem* item;

    if (index == -1)
    {
        item = new QListWidgetItem(ui->eventListWidget);
        ui->eventListWidget->addItem(item);
        ui->eventListWidget->setItemWidget(item, rw);
        // ui->eventListWidget->setCurrentRow(ui->eventListWidget->count()-1);
    }
    else
    {
        item = new QListWidgetItem();
        ui->eventListWidget->insertItem(index, item);
        ui->eventListWidget->setItemWidget(item, rw);
        ui->eventListWidget->setCurrentRow(index);
    }

    // 连接信号
    connect(rw->check, &QCheckBox::stateChanged, this, [=](int){
        bool enable = rw->check->isChecked();
        int row = ui->eventListWidget->row(item);
        settings->setValue("event/r"+QString::number(row)+"Enable", enable);
    });

    connect(rw->eventEdit, &QLineEdit::textChanged, this, [=](const QString& text){
        int row = ui->eventListWidget->row(item);
        settings->setValue("event/r"+QString::number(row)+"Cmd", text);
    });

    connect(rw->actionEdit, &ConditionEditor::textChanged, this, [=]{
        item->setSizeHint(rw->sizeHint());

        QString content = rw->actionEdit->toPlainText();
        int row = ui->eventListWidget->row(item);
        settings->setValue("event/r"+QString::number(row)+"Action", content);
    });

    connect(this, SIGNAL(signalCmdEvent(QString, LiveDanmaku)), rw, SLOT(triggerCmdEvent(QString,LiveDanmaku)));

    connect(rw, &EventWidget::signalEventMsgs, this, [=](QString sl, LiveDanmaku danmaku, bool manual){
        if (!manual && !shallAutoMsg(sl, manual)) // 没有开播，不进行自动回复
        {
            qDebug() << "未开播，不做操作(event)" << sl;
            if (debugPrint)
                localNotify("[未开播，不做操作]");
            return ;
        }
        if (!isLiving() && !manual)
            manual = true;

        QStringList msgs = getEditConditionStringList(sl, danmaku);
        if (msgs.size())
        {
            int r = qrand() % msgs.size();
            QString s = msgs.at(r);
            if (!s.trimmed().isEmpty())
            {
                sendCdMsg(s, 0, EVENT_CD_CN, true, false, manual);
            }
        }
    });

    connect(rw, &EventWidget::signalResized, rw, [=]{
        item->setSizeHint(rw->size());
    });

    remoteControl = settings->value("danmaku/remoteControl", remoteControl).toBool();

    // 设置属性
    rw->check->setChecked(enable);
    rw->eventEdit->setText(cmd);
    rw->actionEdit->setPlainText(action);
    rw->autoResizeEdit();
    rw->adjustSize();
    item->setSizeHint(rw->sizeHint());

    return rw;
}

EventWidget* BaseWindow::addEventAction(MyJson json)
{
    auto item = addEventAction(false, "", "");
    item->fromJson(json);
    return item;
}

void BaseWindow::saveEventList()
{
    settings->setValue("event/count", ui->eventListWidget->count());
    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(row));
        auto tw = static_cast<EventWidget*>(widget);
        settings->setValue("event/r"+QString::number(row)+"Enable", tw->check->isChecked());
        settings->setValue("event/r"+QString::number(row)+"Cmd", tw->eventEdit->text());
        settings->setValue("event/r"+QString::number(row)+"Action", tw->actionEdit->toPlainText());
    }
}

void BaseWindow::restoreEventList()
{
    int count = settings->value("event/count", 0).toInt();
    for (int row = 0; row < count; row++)
    {
        bool enable = settings->value("event/r"+QString::number(row)+"Enable", false).toBool();
        QString key = settings->value("event/r"+QString::number(row)+"Cmd").toString();
        QString event = settings->value("event/r"+QString::number(row)+"Action").toString();
        addEventAction(enable, key, event);
    }
}

bool BaseWindow::hasEvent(QString cmd) const
{
    for (int row = 0; row < ui->eventListWidget->count(); row++)
    {
        auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(row));
        auto tw = static_cast<EventWidget*>(widget);
        if (tw->eventEdit->text() == cmd && tw->check->isChecked())
            return true;
    }
    return false;
}

void BaseWindow::autoSetCookie(QString s)
{
    settings->setValue("danmaku/browserCookie", browserCookie = s);
    if (browserCookie.isEmpty())
        return ;

    userCookies = getCookies();
    getUserInfo();

    // 自动设置弹幕格式
    int posl = browserCookie.indexOf("bili_jct=") + 9;
    if (posl == -1)
        return ;
    int posr = browserCookie.indexOf(";", posl);
    if (posr == -1) posr = browserCookie.length();
    csrf_token = browserCookie.mid(posl, posr - posl);
    qDebug() << "检测到csrf_token:" << csrf_token;

    if (browserData.isEmpty())
        browserData = "color=4546550&fontsize=25&mode=4&msg=&rnd=1605156247&roomid=&bubble=5&csrf_token=&csrf=";
    browserData.replace(QRegularExpression("csrf_token=[^&]*"), "csrf_token=" + csrf_token);
    browserData.replace(QRegularExpression("csrf=[^&]*"), "csrf=" + csrf_token);
    settings->setValue("danmaku/browserData", browserData);
    qDebug() << "设置弹幕格式：" << browserData;
}

QVariant BaseWindow::getCookies()
{
    QList<QNetworkCookie> cookies;

    // 设置cookie
    QString cookieText = browserCookie;
    QStringList sl = cookieText.split(";");
    foreach (auto s, sl)
    {
        s = s.trimmed();
        int pos = s.indexOf("=");
        QString key = s.left(pos);
        QString val = s.right(s.length() - pos - 1);
        cookies.push_back(QNetworkCookie(key.toUtf8(), val.toUtf8()));
    }

    // 请求头里面加入cookies
    QVariant var;
    var.setValue(cookies);
    return var;
}

/**
 * 获取用户信息
 */
void BaseWindow::getUserInfo()
{
    if (browserCookie.isEmpty())
        return ;
    get("http://api.bilibili.com/x/member/web/account", [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            statusLabel->setText(json.value("message").toString());
            qCritical() << s8("账号登录返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        QJsonObject dataObj = json.value("data").toObject();
        cookieUid = snum(static_cast<qint64>(dataObj.value("mid").toDouble()));
        cookieUname = dataObj.value("uname").toString();
        qDebug() << "当前cookie用户：" << cookieUid << cookieUname;
        ui->robotNameLabel->setText(cookieUname);
        statusLabel->setText(cookieUname);
    });
}

/**
 * 获取用户在当前直播间的信息
 * 会触发进入直播间，就不能偷看了……
 */
void BaseWindow::getRoomUserInfo()
{
    /*{
        "code": 0,
        "message": "0",
        "ttl": 1,
        "data": {
            "user_level": {
                "level": 13,
                "next_level": 14,
                "color": 6406234,
                "level_rank": ">50000"
            },
            "vip": {
                "vip": 0,
                "vip_time": "0000-00-00 00:00:00",
                "svip": 0,
                "svip_time": "0000-00-00 00:00:00"
            },
            "title": {
                "title": ""
            },
            "badge": {
                "is_room_admin": false
            },
            "privilege": {
                "target_id": 0,
                "privilege_type": 0,
                "privilege_uname_color": "",
                "buy_guard_notice": null,
                "sub_level": 0,
                "notice_status": 1,
                "expired_time": "",
                "auto_renew": 0,
                "renew_remind": null
            },
            "info": {
                "uid": 20285041,
                "uname": "懒一夕智能科技",
                "uface": "http://i1.hdslb.com/bfs/face/97ae8f0f0e09fbc22fa680c4f5ed93f92678c9eb.jpg",
                "main_rank": 10000,
                "bili_vip": 2,
                "mobile_verify": 1,
                "identification": 1
            },
            "property": {
                "uname_color": "",
                "bubble": 0,
                "danmu": {
                    "mode": 1,
                    "color": 16777215,
                    "length": 20,
                    "room_id": 13328782
                },
                "bubble_color": ""
            },
            "recharge": {
                "status": 0,
                "type": 1,
                "value": "",
                "color": "fb7299",
                "config_id": 0
            },
            "relation": {
                "is_followed": false,
                "is_fans": false,
                "is_in_fansclub": false
            },
            "wallet": {
                "gold": 6100,
                "silver": 3294
            },
            "medal": {
                "cnt": 16,
                "is_weared": false,
                "curr_weared": null,
                "up_medal": {
                    "uid": 358629230,
                    "medal_name": "石乐志",
                    "medal_color": 6067854,
                    "level": 1
                }
            },
            "extra_config": {
                "show_bag": false,
                "show_vip_broadcast": true
            },
            "mailbox": {
                "switch_status": 0,
                "red_notice": 0
            },
            "user_reward": {
                "entry_effect": {
                    "id": 0,
                    "privilege_type": 0,
                    "priority": 0,
                    "web_basemap_url": "",
                    "web_effective_time": 0,
                    "web_effect_close": 0,
                    "web_close_time": 0,
                    "copy_writing": "",
                    "copy_color": "",
                    "highlight_color": "",
                    "mock_effect": 0,
                    "business": 0,
                    "face": "",
                    "basemap_url": "",
                    "show_avatar": 0,
                    "effective_time": 0
                },
                "welcome": {
                    "allow_mock": 1
                }
            },
            "shield_info": {
                "shield_user_list": [],
                "keyword_list": [],
                "shield_rules": {
                    "rank": 0,
                    "verify": 0,
                    "level": 0
                }
            },
            "super_chat_message": {
                "list": []
            },
            "lpl_info": {
                "lpl": 0
            },
            "cd": {
                "guide_free_medal_cost": 0,
                "guide_light_medal": 0,
                "guide_follow": 1,
                "guard_compensate": 0,
                "interact_toasts": []
            }
        }
    }*/

    if (browserCookie.isEmpty())
        return ;
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByUser?room_id=" + roomId;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户在这个房间信息
        QJsonObject info = json.value("data").toObject().value("info").toObject();
        cookieUid = snum(static_cast<qint64>(info.value("uid").toDouble()));
        cookieUname = info.value("uname").toString();
        QString uface = info.value("uface").toString(); // 头像地址
        qDebug() << "当前cookie用户：" << cookieUid << cookieUname;
    });
}

template<class T>
void BaseWindow::showListMenu(QListWidget *listWidget, QString listKey, VoidFunc saveFunc)
{
    QListWidgetItem* item = listWidget->currentItem();
    int row = listWidget->currentRow();

    QString clipText = QApplication::clipboard()->text();
    bool canPaste = false, canContinueCopy = false;
    MyJson clipJson;
    QJsonArray clipArray;
    if (!clipText.isEmpty())
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(clipText.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
        {
            if (doc.isObject())
            {
                clipJson = doc.object();
                JS(clipJson, anchor_key);
                qDebug() << anchor_key << listKey;
                canPaste = (anchor_key == listKey);
            }
            else if (doc.isArray())
            {
                clipArray = doc.array();
            }
            canContinueCopy = true;
        }
    }

    auto moveToRow = [=](int newRow){
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        MyJson json = tw->toJson();
        listWidget->takeItem(row);
        delete item;
        tw->deleteLater();

        auto newTw = new T(listWidget);
        newTw->fromJson(json);

        QListWidgetItem* newItem = new QListWidgetItem;
        listWidget->insertItem(newRow, newItem);
        listWidget->setItemWidget(newItem, newTw);
        newTw->autoResizeEdit();
        newItem->setSizeHint(newTw->sizeHint());

        (this->*saveFunc)();
        listWidget->setCurrentRow(newRow);
    };

    auto menu = new FacileMenu(this);
    menu->addAction("插入 (&a)", [=]{
        if (listKey == CODE_TIMER_TASK_KEY)
            addTimerTask(false, 1800, "", row);
        else if (listKey == CODE_AUTO_REPLY_KEY)
            addAutoReply(false, "", "", row);
        else if (listKey == CODE_EVENT_ACTION_KEY)
            addEventAction(false, "", "", row);
        (this->*saveFunc)();
    })->disable(!item);
    menu->addAction("上移 (&w)", [=]{
        moveToRow(row - 1);
    })->disable(!item || row <= 0);
    menu->addAction("下移 (&s)", [=]{
        moveToRow(row + 1);
    })->disable(!item || row >= listWidget->count()-1);
    menu->split()->addAction("复制 (&c)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        QApplication::clipboard()->setText(tw->toJson().toBa());
    })->disable(!item);
    menu->addAction("继续复制", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        MyJson twJson = tw->toJson();
        if (!clipArray.isEmpty()) // 已经是JSON数组了，继续复制
        {
            QJsonArray array = clipArray;
            array.append(twJson);
            QApplication::clipboard()->setText(QJsonDocument(array).toJson());
        }
        else // 是JSON对象，需要变成数组
        {
            QJsonArray array;
            array.append(clipJson);
            array.append(twJson);
            QApplication::clipboard()->setText(QJsonDocument(array).toJson());
        }
    })->disable(!item)->hide(!canContinueCopy);
    menu->addAction("粘贴 (&v)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);
        tw->fromJson(clipJson);

        (this->*saveFunc)();
    })->disable(!canPaste);
    menu->split()->addAction("删除 (&d)", [=]{
        auto widget = listWidget->itemWidget(item);
        auto tw = static_cast<ListItemInterface*>(widget);

        listWidget->removeItemWidget(item);
        listWidget->takeItem(listWidget->currentRow());

        (this->*saveFunc)();
        tw->deleteLater();
    })->disable(!item);
    menu->exec();
}

void BaseWindow::on_taskListWidget_customContextMenuRequested(const QPoint &)
{
    showListMenu<TaskWidget>(ui->taskListWidget, CODE_TIMER_TASK_KEY, &BaseWindow::saveTaskList);
}

void BaseWindow::on_replyListWidget_customContextMenuRequested(const QPoint &)
{
    showListMenu<ReplyWidget>(ui->replyListWidget, CODE_AUTO_REPLY_KEY, &BaseWindow::saveReplyList);
}

void BaseWindow::on_eventListWidget_customContextMenuRequested(const QPoint &)
{
    showListMenu<EventWidget>(ui->eventListWidget, CODE_EVENT_ACTION_KEY, &BaseWindow::saveEventList);
}

void BaseWindow::on_addTaskButton_clicked()
{
    addTimerTask(false, 1800, "");
    saveTaskList();
    auto widget = ui->taskListWidget->itemWidget(ui->taskListWidget->item(ui->taskListWidget->count()-1));
    auto tw = static_cast<TaskWidget*>(widget);
    ui->taskListWidget->scrollToBottom();
    tw->edit->setFocus();
}

void BaseWindow::on_addReplyButton_clicked()
{
    addAutoReply(false, "", "");
    saveReplyList();
    auto widget = ui->replyListWidget->itemWidget(ui->replyListWidget->item(ui->replyListWidget->count()-1));
    auto rw = static_cast<ReplyWidget*>(widget);
    ui->replyListWidget->scrollToBottom();
    rw->keyEdit->setFocus();
}

void BaseWindow::on_addEventButton_clicked()
{
    addEventAction(false, "", "");
    saveEventList();
    auto widget = ui->eventListWidget->itemWidget(ui->eventListWidget->item(ui->eventListWidget->count()-1));
    auto ew = static_cast<EventWidget*>(widget);
    ui->eventListWidget->scrollToBottom();
    ew->eventEdit->setFocus();
}

void BaseWindow::slotDiange(LiveDanmaku danmaku)
{
    if (danmaku.getMsgType() != MSG_DANMAKU || danmaku.isPkLink() || danmaku.isNoReply())
        return ;
    QRegularExpression re(diangeFormatString);
    QRegularExpressionMatch match;
    if (danmaku.getText().indexOf(re, 0, &match) == -1) // 不是点歌文本
        return ;

    if (match.capturedTexts().size() < 2)
    {
        statusLabel->setText("无法获取点歌内容，请检测点歌格式");
        return ;
    }
    if (ui->diangeNeedMedalCheck->isChecked())
    {
        if (danmaku.getAnchorRoomid() != roomId) // 不是对应的粉丝牌
        {
            qWarning() << "点歌未戴粉丝勋章：" << danmaku.getNickname() << danmaku.getAnchorRoomid() << "!=" << roomId;
            localNotify("点歌未戴粉丝勋章");
            triggerCmdEvent("ORDER_SONG_NO_MEDAL", danmaku);
            return ;
        }
    }

    // 记录到历史（先不复制）
    QString text = match.capturedTexts().at(1);
    text = text.trimmed();
    qDebug() << s8("检测到点歌：") << text;
    diangeHistory.append(Diange{danmaku.getNickname(), danmaku.getUid(), text, danmaku.getTimeline()});
    ui->diangeHistoryListWidget->insertItem(0, text + " - " + danmaku.getNickname());

    if (!diangeAutoCopy) // 是否进行复制操作
        return ;

    if (musicWindow && !musicWindow->isHidden()) // 自动播放
    {
        if (ui->diangeShuaCheck->isChecked() && musicWindow->hasSongInOrder(danmaku.getNickname())) // 已经点了
        {
            localNotify("已阻止频繁点歌");
            triggerCmdEvent("ORDER_SONG_FREQUENCY", danmaku);
        }
        else
        {
            musicWindow->slotSearchAndAutoAppend(text, danmaku.getNickname());
        }
    }
    else
    {
        QClipboard* clip = QApplication::clipboard();
        clip->setText(text);
        triggerCmdEvent("ORDER_SONG_COPY", danmaku);

        addNoReplyDanmakuText(danmaku.getText()); // 点歌不限制长度
        QTimer::singleShot(10, [=]{
            appendNewLiveDanmaku(LiveDanmaku(danmaku.getNickname(), danmaku.getUid(), text, danmaku.getTimeline()));
        });
    }
//    ui->DiangeAutoCopyCheck->setText("点歌（" + text + "）");
}

void BaseWindow::slotSocketError(QAbstractSocket::SocketError error)
{
    qCritical() << "error" << socket->errorString();
    statusLabel->setText(socket->errorString());
}

void BaseWindow::initWS()
{
    socket = new QWebSocket();

    connect(socket, &QWebSocket::connected, this, [=]{
        SOCKET_DEB << "socket connected";
        ui->connectStateLabel->setText("状态：已连接");

        // 5秒内发送认证包
        sendVeriPacket(socket, roomId, token);

        // 定时发送心跳包
        heartTimer->start();
        minuteTimer->start();
    });

    connect(socket, &QWebSocket::disconnected, this, [=]{
        // 正在直播的时候突然断开了
        if (liveStatus)
        {
            qWarning() << "正在直播的时候突然断开，10秒后重连...";
            liveStatus = false;
            // 尝试10秒钟后重连
            connectServerTimer->setInterval(10000);
        }

        SOCKET_DEB << "disconnected";
        ui->connectStateLabel->setText("状态：未连接");
        ui->popularityLabel->setText("");

        heartTimer->stop();
        minuteTimer->stop();

        // 如果不是主动连接的话，这个会断开
        if (!connectServerTimer->isActive())
            connectServerTimer->start();
    });

    connect(socket, &QWebSocket::binaryMessageReceived, this, [=](const QByteArray &message){
//        qDebug() << "binaryMessageReceived" << message;
//        for (int i = 0; i < 100; i++) // 测试内存泄漏
        try {
            slotBinaryMessageReceived(message);

            // 保存到CMDS里
            if (saveRecvCmds && saveCmdsFile)
            {
                QByteArray ba = message;
                ba.replace("\n", "__bmd__n__").replace("\r", "__bmd__r__");
                saveCmdsFile->write(ba);
                saveCmdsFile->write("\n");
            }
        } catch (...) {
            qCritical() << "!!!!!!!error:slotBinaryMessageReceived";
        }

    });

    connect(socket, &QWebSocket::textFrameReceived, this, [=](const QString &frame, bool isLastFrame){
        qDebug() << "textFrameReceived";
    });

    connect(socket, &QWebSocket::textMessageReceived, this, [=](const QString &message){
        qDebug() << "textMessageReceived";
    });

    connect(socket, &QWebSocket::stateChanged, this, [=](QAbstractSocket::SocketState state){
        SOCKET_DEB << "stateChanged" << state;
        QString str = "未知";
        if (state == QAbstractSocket::UnconnectedState)
            str = "未连接";
        else if (state == QAbstractSocket::ConnectingState)
            str = "连接中";
        else if (state == QAbstractSocket::ConnectedState)
            str = "已连接";
        else if (state == QAbstractSocket::BoundState)
            str = "已绑定";
        else if (state == QAbstractSocket::ClosingState)
            str = "断开中";
        else if (state == QAbstractSocket::ListeningState)
            str = "监听中";
        ui->connectStateLabel->setText(str);
    });

    heartTimer = new QTimer(this);
    heartTimer->setInterval(30000);
    connect(heartTimer, &QTimer::timeout, this, [=]{
        if (socket->state() == QAbstractSocket::ConnectedState)
            sendHeartPacket();
        else if (socket->state() == QAbstractSocket::UnconnectedState)
            startConnectRoom();

        // PK Socket
        if (pkSocket && pkSocket->state() == QAbstractSocket::ConnectedState)
        {
            QByteArray ba;
            ba.append("[object Object]");
            ba = makePack(ba, HEARTBEAT);
            pkSocket->sendBinaryMessage(ba);
        }

        // 机器人 Socket
        for (int i = 0; i < robots_sockets.size(); i++)
        {
            QWebSocket* socket = robots_sockets.at(i);
            if (socket->state() == QAbstractSocket::ConnectedState)
            {
                QByteArray ba;
                ba.append("[object Object]");
                ba = makePack(ba, HEARTBEAT);
                socket->sendBinaryMessage(ba);
            }
        }
    });

    xliveHeartBeatTimer = new QTimer(this);
    xliveHeartBeatTimer->setInterval(59500);
    connect(xliveHeartBeatTimer, &QTimer::timeout, this, [=]{
        if (isLiving())
            sendXliveHeartBeatX();
    });
}

void BaseWindow::startConnectRoom()
{
    if (roomId.isEmpty())
        return ;

    // 初始化主播数据
    currentFans = 0;
    currentFansClub = 0;
    popularVal = 2;

    // 准备房间数据
    if (danmakuCounts)
        danmakuCounts->deleteLater();
    QDir dir;
    dir.mkdir(dataPath+"danmaku_counts");
    danmakuCounts = new QSettings(dataPath+"danmaku_counts/" + roomId + ".ini", QSettings::Format::IniFormat);
    if (ui->calculateDailyDataCheck->isChecked())
        startCalculateDailyData();

    // 开始获取房间信息
    getRoomInfo(true);
    if (ui->enableBlockCheck->isChecked())
        refreshBlockList();
}

void BaseWindow::sendXliveHeartBeatE()
{
    if (roomId.isEmpty() || cookieUid.isEmpty() || !isLiving())
        return ;
    xliveHeartBeatIndex = 0;

    QString url("https://live-trace.bilibili.com/xlive/data-interface/v1/x25Kn/E");

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "id=" + QString("[%1,%2,%3,%4]")
             .arg(parentAreaId).arg(areaId).arg(xliveHeartBeatIndex).arg(roomId);
    datas << "device=[\"AUTO4115984068636104\",\"f5f08e2f-e4e3-4156-8127-616f79a17e1a\"]";
    datas << "ts=" + snum(QDateTime::currentMSecsSinceEpoch());
    datas << "is_patch=0";
    datas << "heart_beat=[]";
    datas << "ua=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.146 Safari/537.36";
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";
    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString message = json.value("message").toString();
            statusLabel->setText(message);
            qCritical() << s8("warning: 发送失败：") << message << datas.join("&");
        }

        /*{
            "code": 0,
            "message": "0",
            "ttl": 1,
            "data": {
                "timestamp": 1612765538,
                "heartbeat_interval": 60,
                "secret_key": "seacasdgyijfhofiuxoannn",
                "secret_rule": [2,5,1,4],
                "patch_status": 2
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        xliveHeartBeatBenchmark = data.value("secret_key").toString();
        xliveHeartBeatEts = qint64(data.value("timestamp").toDouble());
        xliveHeartBeatInterval = data.value("heartbeat_interval").toInt();
        xliveHeartBeatSecretRule = data.value("secret_rule").toArray();

        xliveHeartBeatTimer->start();
        xliveHeartBeatTimer->setInterval(xliveHeartBeatInterval * 1000 - 1000);
    });
}

void BaseWindow::sendXliveHeartBeatX()
{
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // 获取加密的数据
    QJsonObject postData;
    postData.insert("id",  QString("[%1,%2,%3,%4]")
                    .arg(parentAreaId).arg(areaId).arg(++xliveHeartBeatIndex).arg(roomId));
    postData.insert("device", "[\"AUTO4115984068636104\",\"f5f08e2f-e4e3-4156-8127-616f79a17e1a\"]");
    postData.insert("ts", timestamp);
    postData.insert("ets", xliveHeartBeatEts);
    postData.insert("benchmark", xliveHeartBeatBenchmark);
    postData.insert("time", xliveHeartBeatInterval);
    postData.insert("ua", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.146 Safari/537.36");
    postData.insert("csrf_token", csrf_token);
    postData.insert("csrf", csrf_token);
    QJsonObject calcText;
    calcText.insert("t", postData);
    calcText.insert("r", xliveHeartBeatSecretRule);

    post(encServer, QJsonDocument(calcText).toJson(), [=](QNetworkReply* reply){
        QByteArray repBa = reply->readAll();
        // qDebug() << "加密直播心跳包数据返回：" << repBa;
        sendXliveHeartBeatX(QString(repBa), timestamp);
    });
}

void BaseWindow::sendXliveHeartBeatX(QString s, qint64 timestamp)
{
    QString url("https://live-trace.bilibili.com/xlive/data-interface/v1/x25Kn/X");

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "s=" + s;
    datas << "id=" + QString("[%1,%2,%3,%4]")
             .arg(parentAreaId).arg(areaId).arg(xliveHeartBeatIndex).arg(roomId);
    datas << "device=[\"AUTO4115984068636104\",\"f5f08e2f-e4e3-4156-8127-616f79a17e1a\"]";
    datas << "ets=" + snum(xliveHeartBeatEts);
    datas << "benchmark=" + xliveHeartBeatBenchmark;
    datas << "time=" + snum(xliveHeartBeatInterval);
    datas << "ts=" + snum(timestamp);
    datas << "ua=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.146 Safari/537.36";
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";
    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString message = json.value("message").toString();
            statusLabel->setText(message);
            qCritical() << s8("warning: 发送直播心跳失败：") << message << datas.join("&");
            return ;
        }

        /*{
            "code": 0,
            "message": "0",
            "ttl": 1,
            "data": {
                "heartbeat_interval": 60,
                "timestamp": 1612765598,
                "secret_rule": [2,5,1,4],
                "secret_key": "seacasdgyijfhofiuxoannn"
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        xliveHeartBeatBenchmark = data.value("secret_key").toString();
        xliveHeartBeatEts = qint64(data.value("timestamp").toDouble());
        xliveHeartBeatInterval = data.value("heartbeat_interval").toInt();
        xliveHeartBeatSecretRule = data.value("secret_rule").toArray();
    });
}

/**
 * 获取房间信息
 * （已废弃）
 */
void BaseWindow::getRoomInit()
{
    QString roomInitUrl = "https://api.live.bilibili.com/room/v1/Room/room_init?id=" + roomId;
    connect(new NetUtil(roomInitUrl), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "获取房间初始化出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qCritical() << "返回结果不为0：" << json.value("message").toString();
            return ;
        }

        int realRoom = json.value("data").toObject().value("room_id").toInt();
        qDebug() << "真实房间号：" << realRoom;
    });
}

void BaseWindow::getRoomInfo(bool reconnect)
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + roomId;
    get(url, [=](QJsonObject json) {
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject dataObj = json.value("data").toObject();
        QJsonObject roomInfo = dataObj.value("room_info").toObject();
        QJsonObject anchorInfo = dataObj.value("anchor_info").toObject();

        // 获取房间信息
        roomId = QString::number(roomInfo.value("room_id").toInt()); // 应当一样，但防止是短ID
        ui->roomIdEdit->setText(roomId);
        shortId = QString::number(roomInfo.value("short_id").toInt());
        upUid = QString::number(static_cast<qint64>(roomInfo.value("uid").toDouble()));
        liveStatus = roomInfo.value("live_status").toInt();
        int pkStatus = roomInfo.value("pk_status").toInt();
        if (danmakuWindow)
            danmakuWindow->setIds(upUid.toLongLong(), roomId.toLongLong());
        roomTitle = roomInfo.value("title").toString();
        upName = anchorInfo.value("base_info").toObject().value("uname").toString();
        setWindowTitle(roomTitle + " - " + upName);
        tray->setToolTip(roomTitle + " - " + upName);
        ui->roomNameLabel->setText(roomTitle + " - " + upName);
        if (liveStatus == 0)
        {
            ui->popularityLabel->setText("未开播");
            if (ui->timerConnectServerCheck->isChecked() && !connectServerTimer->isActive())
                connectServerTimer->start();
        }
        else if (liveStatus == 1)
        {
            ui->popularityLabel->setText("已开播");
        }
        else if (liveStatus == 2)
        {
            ui->popularityLabel->setText("轮播中");
        }
        else
        {
            ui->popularityLabel->setText("未知状态" + snum(liveStatus));
        }

        qDebug() << "房间信息: roomid=" << roomId
                 << "  shortid=" << shortId
                 << "  upName=" << upName
                 << "  uid=" << upUid;

        if (pkStatus)
        {
            QJsonObject battleInfo = dataObj.value("battle_info").toObject();
            QString pkId = QString::number(static_cast<qint64>(battleInfo.value("pk_id").toDouble()));
            if (pkId.toLongLong() > 0 && reconnect)
            {
                // 这个 pk_status 不是 battle_type
                pking = true;
                // pkVideo = pkStatus == 2; // 注意：如果是匹配到后、开始前，也算是1/2,
                getPkInfoById(roomId, pkId);
                qDebug() << "正在大乱斗：" << pkId << "   pk_status=" << pkStatus;
            }
        }
        else
        {
            QTimer::singleShot(5000, [=]{ // 延迟5秒，等待主播UID和机器人UID都获取到
                if (cookieUid == upUid)
                    ui->actionJoin_Battle->setEnabled(true);
            });
        }

        areaId = snum(roomInfo.value("area_id").toInt());
        areaName = roomInfo.value("area_name").toString();
        parentAreaId = snum(roomInfo.value("parent_area_id").toInt());
        parentAreaName = roomInfo.value("parent_area_name").toString();

        // 获取主播信息
        currentFans = anchorInfo.value("relation_info").toObject().value("attention").toInt();
        currentFansClub = anchorInfo.value("medal_info").toObject().value("fansclub").toInt();
//        qDebug() << s8("粉丝数：") << currentFans << s8("    粉丝团：") << currentFansClub;
        fansLabel->setText("粉丝:" + snum(currentFans));
        // getFansAndUpdate();

        // 异步获取房间封面
        getRoomCover(roomInfo.value("cover").toString());

        // 获取主播头像
        getUpFace(upUid);

        // 判断房间，未开播则暂停连接，等待开播
        if (!isLivingOrMayliving())
            return ;

        // 开始工作
        if (isLiving())
            slotStartWork();

        if (!reconnect)
            return ;

        // 获取弹幕信息
        getDanmuInfo();

        // 录播
        if (ui->recordCheck->isChecked() && isLiving())
            startLiveRecord();
    });

    if (reconnect)
        ui->connectStateLabel->setText("获取房间信息...");
}

bool BaseWindow::isLivingOrMayliving()
{
    if (ui->timerConnectServerCheck->isChecked())
    {
        if (!isLiving()) // 未开播，等待下一次的检测
        {
            // 如果是开播前一段时间，则继续保持着连接
            int start = ui->startLiveHourSpin->value();
            int end = ui->endLiveHourSpin->value();
            int hour = QDateTime::currentDateTime().time().hour();
            int minu = QDateTime::currentDateTime().time().minute();
            bool abort = false;
            qint64 currentVal = hour * 3600000 + minu * 60000;
            qint64 nextVal = (currentVal + CONNECT_SERVER_INTERVAL) % (24 * 3600000); // 0点
            if (start < end) // 白天档
            {
                qDebug() << "白天档" << currentVal << start * 3600000 << end * 3600000;
                if (nextVal >= start * 3600000
                        && currentVal <= end * 3600000)
                {
                    if (ui->doveCheck->isChecked()) // 今天鸽了
                    {
                        abort = true;
                        qDebug() << "今天鸽了";
                    }
                    // 即将开播
                }
                else // 直播时间之外
                {
                    qDebug() << "未开播，继续等待";
                    abort = true;
                    if (currentVal > end * 3600000 && ui->doveCheck->isChecked()) // 今天结束鸽鸽
                        ui->doveCheck->setChecked(false);
                }
            }
            else if (start > end) // 熬夜档
            {
                qDebug() << "晚上档" << currentVal << start * 3600000 << end * 3600000;
                if (currentVal + CONNECT_SERVER_INTERVAL >= start * 3600000
                        || currentVal <= end * 3600000)
                {
                    if (ui->doveCheck->isChecked()) // 今晚鸽了
                        abort = true;
                    // 即将开播
                }
                else // 直播时间之外
                {
                    qDebug() << "未开播，继续等待";
                    abort = true;
                    if (currentVal > end * 3600000 && currentVal < start * 3600000
                            && ui->doveCheck->isChecked())
                        ui->doveCheck->setChecked(false);
                }
            }

            if (abort)
            {
                qDebug() << "短期内不会开播，暂且断开连接";
                if (!connectServerTimer->isActive())
                    connectServerTimer->start();
                ui->connectStateLabel->setText("等待连接");

                // 如果正在连接或打算连接，却未开播，则断开
                if (socket->state() != QAbstractSocket::UnconnectedState)
                    socket->close();
                return false;
            }
        }
        else // 已开播，则停下
        {
            qDebug() << "开播，停止定时连接";
            if (connectServerTimer->isActive())
                connectServerTimer->stop();
            if (ui->doveCheck->isChecked())
                ui->doveCheck->setChecked(false);
            return true;
        }
    }
    return true;
}

void BaseWindow::getRoomCover(QString url)
{
    get(url, [=](QNetworkReply* reply1){
        QByteArray jpegData = reply1->readAll();

        QPixmap pixmap;
        pixmap.loadFromData(jpegData);
        roomCover = pixmap; // 原图
        if (roomCover.isNull())
            return ;

        int w = ui->roomCoverLabel->width();
        if (w > ui->tabWidget->contentsRect().width())
            w = ui->tabWidget->contentsRect().width();
        pixmap = pixmap.scaledToWidth(w, Qt::SmoothTransformation);
        ui->roomCoverLabel->setPixmap(getRoundedPixmap(pixmap));
        ui->roomCoverLabel->setMinimumSize(1, 1);

        // 设置程序主题
        QColor bg, fg, sbg, sfg;
        auto colors = ImageUtil::extractImageThemeColors(roomCover.toImage(), 7);
        ImageUtil::getBgFgSgColor(colors, &bg, &fg, &sbg, &sfg);
        prevPa = BFSColor::fromPalette(palette());
        currentPa = BFSColor(QList<QColor>{bg, fg, sbg, sfg});
        QPropertyAnimation* ani = new QPropertyAnimation(this, "paletteProg");
        ani->setStartValue(0);
        ani->setEndValue(1.0);
        ani->setDuration(500);
        connect(ani, &QPropertyAnimation::valueChanged, this, [=](const QVariant& val){
            double d = val.toDouble();
            BFSColor bfs = prevPa + (currentPa - prevPa) * d;
            QColor bg, fg, sbg, sfg;
            bfs.toColors(&bg, &fg, &sbg, &sfg);

            QPalette pa;
            pa.setColor(QPalette::Window, bg);
            pa.setColor(QPalette::Background, bg);
            pa.setColor(QPalette::Button, bg);
            pa.setColor(QPalette::Base, bg);

            pa.setColor(QPalette::Foreground, fg);
            pa.setColor(QPalette::Text, fg);
            pa.setColor(QPalette::ButtonText, fg);
            pa.setColor(QPalette::WindowText, fg);
            statusLabel->setStyleSheet("color:" + QVariant(fg).toString());
            fansLabel->setStyleSheet("color:" + QVariant(fg).toString());
            rankLabel->setStyleSheet("color:" + QVariant(fg).toString());

            pa.setColor(QPalette::Highlight, sbg);
            pa.setColor(QPalette::HighlightedText, sfg);
            setPalette(pa);
            setStyleSheet("QMainWindow{background:"+QVariant(bg).toString()+"} QLabel QCheckBox{background: transparent; color:"+QVariant(fg).toString()+"}");
            ui->menubar->setStyleSheet("QMenuBar:item{background:transparent;}QMenuBar{background:transparent; color:"+QVariant(sbg).toString()+"}");
        });
        connect(ani, SIGNAL(finished()), ani, SLOT(deleteLater()));
        ani->start();

        // 设置主要界面主题
        ui->tabWidget->setBg(roomCover);
    });
}

QPixmap BaseWindow::getRoundedPixmap(QPixmap pixmap) const
{
    QPixmap dest(pixmap.size());
    dest.fill(Qt::transparent);
    QPainter painter(&dest);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QRect rect = QRect(0, 0, pixmap.width(), pixmap.height());
    QPainterPath path;
    path.addRoundedRect(rect, 5, 5);
    painter.setClipPath(path);
    painter.drawPixmap(rect, pixmap);
    return dest;
}

void BaseWindow::getUpFace(QString uid)
{
    QString url = "http://api.bilibili.com/x/space/acc/info?mid=" + uid;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        QString face = data.value("face").toString();

        // 开始下载头像
        getUpPortrait(face);
    });
}

void BaseWindow::getUpPortrait(QString faceUrl)
{
    get(faceUrl, [=](QNetworkReply* reply){
        QByteArray jpegData = reply->readAll();

        QPixmap pixmap;
        pixmap.loadFromData(jpegData);

        // 设置成圆角
        int side = qMin(pixmap.width(), pixmap.height());
        upFace = QPixmap(side, side);
        upFace.fill(Qt::transparent);
        QPainter painter(&upFace);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addEllipse(0, 0, side, side);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, side, side, pixmap);

        // 设置到程序
        QPixmap face = isLiving() ? getLivingPixmap(upFace) : upFace;
        setWindowIcon(face);
        tray->setIcon(face);
    });
}

/**
 * 给直播中的头像添加绿点
 */
QPixmap BaseWindow::getLivingPixmap(QPixmap pixmap) const
{
    if (pixmap.isNull())
        return pixmap;
    QPainter painter(&pixmap);
    int wid = qMin(pixmap.width(), pixmap.height()) / 3;
    QRect rect(pixmap.width() - wid, pixmap.height() - wid, wid, wid);
    QPainterPath path;
    path.addEllipse(rect);
    painter.fillPath(path, Qt::green);
    return pixmap;
}

/**
 * 这是真正开始连接的
 */
void BaseWindow::getDanmuInfo()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo?id="+roomId+"&type=0";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;
        reply->deleteLater();

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "获取弹幕信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        token = data.value("token").toString();
        QJsonArray hostArray = data.value("host_list").toArray();
        hostList.clear();
        foreach (auto val, hostArray)
        {
            QJsonObject o = val.toObject();
            hostList.append(HostInfo{
                                o.value("host").toString(),
                                o.value("port").toInt(),
                                o.value("wss_port").toInt(),
                                o.value("ws_port").toInt(),
                            });
        }
        SOCKET_DEB << s8("getDanmuInfo: host数量=") << hostList.size() << "  token=" << token;

        startMsgLoop();

        updateExistGuards(0);
        updateOnlineGoldRank();
    });
    manager->get(*request);
    ui->connectStateLabel->setText("获取弹幕信息...");
}

void BaseWindow::getFansAndUpdate()
{
    QString url = "http://api.bilibili.com/x/relation/followers?vmid=" + upUid;
    get(url, [=](QJsonObject json){
        QJsonArray list = json.value("data").toObject().value("list").toArray();
        QList<FanBean> newFans;
        foreach (QJsonValue val, list)
        {
            QJsonObject fan = val.toObject();
            qint64 mid = static_cast<qint64>(fan.value("mid").toDouble());
            int attribute = fan.value("attribute").toInt(); // 三位数：1-1-0 被关注-已关注-未知
            if (attribute == 0) // 0是什么意思啊，不懂诶……应该是关注吧？
                attribute = 2;
            qint64 mtime = static_cast<qint64>(fan.value("mtime").toDouble()); // 这是秒吧？
            QString uname = fan.value("uname").toString();

            newFans.append(FanBean{mid, uname, attribute & 2, mtime});
//qDebug() << "    粉丝信息：" << mid << uname << attribute << QDateTime::fromSecsSinceEpoch(mtime);
        }

//qDebug() << "    现有粉丝数(第一页)：" << newFans.size();

        // 第一次加载
        if (!fansList.size())
        {
            fansList = newFans;
            return ;
        }

        // 进行比较 新增关注 的 取消关注
        // 先找到原先最后关注并且还在的，即：旧列.index == 新列.find
        int index = -1; // 旧列索引
        int find = -1;  // 新列索引
        while (++index < fansList.size())
        {
            FanBean fan = fansList.at(index);
            for (int j = 0; j < newFans.size(); j++)
            {
                FanBean nFan = newFans.at(j);
                if (fan.mid == nFan.mid)
                {
                    find = j;
                    break;
                }
            }
            if (find >= 0)
                break;
        }
//qDebug() << ">>>>>>>>>>" << "index:" << index << "           find:" << find;

        // 取消关注（只支持最新关注的，不是专门做的，只是顺带）
        if (!fansList.size())
        {}
        else if (index >= fansList.size()) // 没有被关注过，或之前关注的全部取关了？
        {
            qDebug() << s8("没有被关注过，或之前关注的全部取关了？");
        }
        else // index==0没有人取关，index>0则有人取关
        {
            for (int i = 0; i < index; i++)
            {
                FanBean fan = fansList.at(i);
                qDebug() << s8("取消关注：") << fan.uname << QDateTime::fromSecsSinceEpoch(fan.mtime);
                appendNewLiveDanmaku(LiveDanmaku(fan.uname, fan.mid, false, QDateTime::fromSecsSinceEpoch(fan.mtime)));
            }
            while (index--)
                fansList.removeFirst();
        }

        // 新增关注
        for (int i = find-1; i >= 0; i--)
        {
            FanBean fan = newFans.at(i);
            qDebug() << s8("新增关注：") << fan.uname << QDateTime::fromSecsSinceEpoch(fan.mtime);
            LiveDanmaku danmaku(fan.uname, fan.mid, true, QDateTime::fromSecsSinceEpoch(fan.mtime));
            appendNewLiveDanmaku(danmaku);

            if (i == 0) // 只发送第一个（其他几位，对不起了……）
            {
                if (!justStart && ui->autoSendAttentionCheck->isChecked())
                {
                    sendAttentionThankIfNotRobot(danmaku);
                }
                else
                {
                    judgeRobotAndMark(danmaku);
                }
            }
            else
            {
                judgeRobotAndMark(danmaku);
            }

            fansList.insert(0, fan);
        }

    });
}

void BaseWindow::getPkInfoById(QString roomId, QString pkId)
{
    /*{
        "code": 0,
        "msg": "ok",
        "message": "ok",
        "data": {
            "battle_type": 2,
            "match_type": 8,
            "status_msg": "",
            "pk_id": 200456233,
            "season_id": 31,
            "status": 201,
            "match_status": 0,
            "match_max_time": 300,
            "match_end_time": 0,
            "pk_pre_time": 1611844801,
            "pk_start_time": 1611844811,
            "pk_frozen_time": 1611845111,
            "pk_end_time": 1611845121,
            "timestamp": 1611844942,
            "final_hit_votes": 0,
            "pk_votes_add": 0,
            "pk_votes_name":"乱斗值",
            "pk_votes_type": 0,
            "cdn_id": 72,
            "init_info": {
                "room_id": 22580649,
                "uid": 356772517,
                "uname":"呱什么呱",
                "face": "https://i1.hdslb.com/bfs/face/3a4d357fdf88d73110b6b0cb31f3417f70c785af.jpg",
                "votes": 0,
                "final_hit_status": 0,
                "resist_crit_status": 0,
                "resist_crit_num": "",
                "best_uname": "",
                "winner_type": 0,
                "end_win_task": null
            },
            "match_info": {
                "room_id": 4720666,
                "uid": 13908357,
                "uname":"娇羞的蘑菇",
                "face": "https://i1.hdslb.com/bfs/face/180d0e87a0e88cb6c04ce6504c3f04003dd77392.jpg",
                "votes": 0,
                "final_hit_status": 0,
                "resist_crit_status": 0,
                "resist_crit_num": "",
                "best_uname": "",
                "winner_type": 0,
                "end_win_task": null
            }
        }
    }*/

    QString url = "https://api.live.bilibili.com/av/v1/Battle/getInfoById?pk_id="+pkId+"&roomid=" + roomId;\
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qDebug() << "PK查询结果不为0：" << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        // pk_pre_time  pk_start_time  pk_frozen_time  pk_end_time
        QJsonObject pkData = json.value("data").toObject();
        qint64 pkStartTime = static_cast<qint64>(pkData.value("pk_start_time").toDouble());
        pkEndTime = static_cast<qint64>(pkData.value("pk_frozen_time").toDouble());

        QJsonObject initInfo = pkData.value("init_info").toObject();
        QJsonObject matchInfo = pkData.value("match_info").toObject();
        if (snum(qint64(matchInfo.value("room_id").toDouble())) == roomId)
        {
            QJsonObject temp = initInfo;
            initInfo = matchInfo;
            matchInfo = temp;
        }

        pkRoomId = snum(qint64(matchInfo.value("room_id").toDouble()));
        pkUid = snum(qint64(matchInfo.value("uid").toDouble()));
        pkUname = matchInfo.value("uname").toString();
        myVotes = initInfo.value("votes").toInt();
        matchVotes = matchInfo.value("votes").toInt();
        pkVideo = pkData.value("battle_type").toInt() == 2;

        pkTimer->start();
        if (danmakuWindow)
        {
            danmakuWindow->showStatusText();
            danmakuWindow->setToolTip(pkUname);
            danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
        }
        ui->actionShow_PK_Video->setEnabled(true);

        qint64 currentTime = QDateTime::currentSecsSinceEpoch();
        // 已经开始大乱斗
        if (currentTime > pkStartTime) // !如果是刚好开始，可能不能运行下面的，也可能不会触发"PK_START"，不管了
        {
            if (pkChuanmenEnable)
            {
                connectPkRoom();
            }

            // 监听尾声
            qint64 deltaEnd = pkEndTime - currentTime;
            QTimer::singleShot(qMax(0, int(deltaEnd*1000 - pkJudgeEarly)), [=]{
                if (!pking || roomId != this->roomId) // 比如换房间了
                {
                    qDebug() << "大乱斗结束前，逻辑不正确" << pking << roomId
                             << QDateTime::currentSecsSinceEpoch() << pkEndTime;
                    return ;
                }
                slotPkEnding();
            });
            QTimer::singleShot(deltaEnd, [=]{
                pkEnding = false;
                pkVoting = 0;
            });
        }
    });
}

/**
 * socket连接到直播间，但不一定已开播
 */
void BaseWindow::startMsgLoop()
{
    // 保存房间弹幕
    if (ui->saveDanmakuToFileCheck && !danmuLogFile)
        startSaveDanmakuToFile();

    int hostRetry = 0; // 循环测试连接（意思一下，暂时未使用，否则应当设置为成员变量）
    HostInfo hostServer = hostList.at(hostRetry);
    QString host = QString("wss://%1:%2/sub").arg(hostServer.host).arg(hostServer.wss_port);
    SOCKET_DEB << "hostServer:" << host;

    // 设置安全套接字连接模式（不知道有啥用）
    QSslConfiguration config = socket->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    config.setProtocol(QSsl::TlsV1SslV3);
    socket->setSslConfiguration(config);

    socket->open(host);
}

/**
 * 给body加上头部信息
偏移量	长度	类型	含义
0	4	uint32	封包总大小（头部大小+正文大小）
4	2	uint16	头部大小（一般为0x0010，16字节）
6	2	uint16	协议版本:0普通包正文不使用压缩，1心跳及认证包正文不使用压缩，2普通包正文使用zlib压缩
8	4	uint32	操作码（封包类型）
12	4	uint32	sequence，可以取常数1
 */
QByteArray BaseWindow::makePack(QByteArray body, qint32 operation)
{
    // 因为是大端，所以要一个个复制
    qint32 totalSize = 16 + body.size();
    short headerSize = 16;
    short protover = 1;
    qint32 seqId = 1;

    auto byte4 = [=](qint32 i) -> QByteArray{
        QByteArray ba(4, 0);
        ba[3] = (uchar)(0x000000ff & i);
        ba[2] = (uchar)((0x0000ff00 & i) >> 8);
        ba[1] = (uchar)((0x00ff0000 & i) >> 16);
        ba[0] = (uchar)((0xff000000 & i) >> 24);
        return ba;
    };

    auto byte2 = [=](short i) -> QByteArray{
        QByteArray ba(2, 0);
        ba[1] = (uchar)(0x00ff & i);
        ba[0] = (uchar)((0xff00 & i) >> 8);
        return ba;
    };

    QByteArray header;
    header += byte4(totalSize);
    header += byte2(headerSize);
    header += byte2(protover);
    header += byte4(operation);
    header += byte4(seqId);

    return header + body;


    /* // 小端算法，直接上结构体
    int totalSize = 16 + body.size();
    short headerSize = 16;
    short protover = 1;
    int seqId = 1;

    HeaderStruct header{totalSize, headerSize, protover, operation, seqId};

    QByteArray ba((char*)&header, sizeof(header));
    return ba + body;*/
}

void BaseWindow::sendVeriPacket(QWebSocket* socket, QString roomId, QString token)
{
    QByteArray ba;
    ba.append("{\"uid\": 0, \"roomid\": "+roomId+", \"protover\": 2, \"platform\": \"web\", \"clientver\": \"1.14.3\", \"type\": 2, \"key\": \""+token+"\"}");
    ba = makePack(ba, AUTH);
    SOCKET_DEB << "发送认证包：" << ba;
    socket->sendBinaryMessage(ba);
}

/**
 * 每隔半分钟发送一次心跳包
 */
void BaseWindow::sendHeartPacket()
{
    QByteArray ba;
    ba.append("[object Object]");
    ba = makePack(ba, HEARTBEAT);
//    SOCKET_DEB << "发送心跳包：" << ba;
    socket->sendBinaryMessage(ba);
}

QString BaseWindow::getLocalNickname(qint64 uid) const
{
    if (localNicknames.contains(uid))
        return localNicknames.value(uid);
    return "";
}

void BaseWindow::analyzeMsgAndCd(QString& msg, int &cd, int &channel) const
{
    QRegularExpression re("^\\s*\\(cd(\\d{1,2}):(\\d+)\\)");
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) == -1)
        return ;
    QStringList caps = match.capturedTexts();
    QString full = caps.at(0);
    QString chann = caps.at(1);
    QString val = caps.at(2);
    msg = msg.right(msg.length() - full.length());
    cd = val.toInt() * 1000;
    channel = chann.toInt();
}

/**
 * 支持变量名
 */
QString BaseWindow::processTimeVariants(QString msg) const
{
    // 早上/下午/晚上 - 好呀
    int hour = QTime::currentTime().hour();
    if (msg.contains("%hour%"))
    {
        QString rst;
        if (hour <= 3)
            rst = "晚上";
        else if (hour <= 5)
            rst = "凌晨";
        else if (hour < 11)
            rst = "早上";
        else if (hour <= 13)
            rst = "中午";
        else if (hour <= 17)
            rst = "下午";
        else if (hour <= 24)
            rst = "晚上";
        else
            rst = "今天";
        msg = msg.replace("%hour%", rst);
    }

    // 早上好、早、晚上好-
    if (msg.contains("%greet%"))
    {
        QStringList rsts;
        rsts << "您好" << "你好";
        if (hour <= 3)
            rsts << "晚上好";
        else if (hour <= 5)
            rsts << "凌晨好";
        else if (hour < 11)
            rsts << "早上好" << "早" << "早安";
        else if (hour <= 13)
            rsts << "中午好";
        else if (hour <= 16)
            rsts << "下午好";
        else if (hour <= 24)
            rsts << "晚上好";
        int r = qrand() % rsts.size();
        msg = msg.replace("%greet%", rsts.at(r));
    }

    // 全部打招呼
    if (msg.contains("%all_greet%"))
    {
        QStringList sl{"啊", "呀"};
        int r = qrand() % sl.size();
        QString tone = sl.at(r);
        QStringList rsts;
        rsts << "您好"+tone
             << "你好"+tone;
        if (hour <= 3)
            rsts << "晚上好"+tone;
        else if (hour <= 5)
            rsts << "凌晨好"+tone;
        else if (hour < 11)
            rsts << "早上好"+tone << "早"+tone;
        else if (hour <= 13)
            rsts << "中午好"+tone;
        else if (hour <= 16)
            rsts << "下午好"+tone;
        else if (hour <= 24)
            rsts << "晚上好"+tone;

        if (hour >= 6 && hour <= 8)
            rsts << "早饭吃了吗";
        else if (hour >= 11 && hour <= 12)
            rsts << "午饭吃了吗";
        else if (hour >= 17 && hour <= 18)
            rsts << "晚饭吃了吗";

        if ((hour >= 23 && hour <= 24)
                || (hour >= 0 && hour <= 3))
            rsts << "还没睡"+tone << "怎么还没睡";
        else if (hour >= 3 && hour <= 5)
            rsts << "通宵了吗";

        r = qrand() % rsts.size();
        msg = msg.replace("%all_greet%", rsts.at(r));
    }

    // 语气词
    if (msg.contains("%tone%"))
    {
        QStringList sl{"啊", "呀"};
        int r = qrand() % sl.size();
        msg = msg.replace("%tone%", sl.at(r));
    }
    if (msg.contains("%lela%"))
    {
        QStringList sl{"了", "啦"};
        int r = qrand() % sl.size();
        msg = msg.replace("%lela%", sl.at(r));
    }

    // 标点
    if (msg.contains("%punc%"))
    {
        QStringList sl{"~", "！"};
        int r = qrand() % sl.size();
        msg = msg.replace("%punc%", sl.at(r));
    }

    // 语气词或标点
    if (msg.contains("%tone/punc%"))
    {
        QStringList sl{"啊", "呀", "~", "！"};
        int r = qrand() % sl.size();
        msg = msg.replace("%tone/punc%", sl.at(r));
    }

    return msg;
}

QStringList BaseWindow::getEditConditionStringList(QString plainText, LiveDanmaku user)
{
    plainText = processDanmakuVariants(plainText, user);
    CALC_DEB << "处理变量之后：" << plainText;
    lastConditionDanmu = plainText;

    QStringList lines = plainText.split("\n", QString::SkipEmptyParts);
    QStringList result;
    // 替换变量，寻找条件
    for (int i = 0; i < lines.size(); i++)
    {
        QString line = lines.at(i);
        line = processMsgHeaderConditions(line);
        CALC_DEB << "取条件后：" << line << "    原始：" << lines.at(i);
        if (!line.isEmpty())
            result.append(line.trimmed());
    }

    // 判断超过长度的
    if (removeLongerRandomDanmaku)
    {
        for (int i = 0; i < result.size() && result.size() > 1; i++)
        {
            QString s = result.at(i);
            s = s.replace(QRegExp("\\(\\s*cd\\d+\\s*:\\s*\\d+\\s*\\)"), "").replace("*", "").trimmed();
            if (!s.contains(">") && !s.contains("\\n") && s.length() > danmuLongest && !s.contains("%"))
            {
                if (debugPrint)
                    localNotify("[去掉过长候选：" + s + "]");
                result.removeAt(i--);
            }
        }
    }

    // 查看是否优先级
    auto hasPriority = [=](const QStringList& sl){
        for (int i = 0; i < sl.size(); i++)
            if (sl.at(i).startsWith("*"))
                return true;
        return false;
    };

    // 去除优先级
    while (hasPriority(result))
    {
        for (int i = 0; i < result.size(); i++)
        {
            if (!result.at(i).startsWith("*"))
                result.removeAt(i--);
            else
                result[i] = result.at(i).right(result.at(i).length()-1);
        }
    }
    CALC_DEB << "condition result:" << result;
    lastCandidateDanmaku = result.join("\n");

    return result;
}

/**
 * 处理用户信息中蕴含的表达式
 * 用户信息、弹幕、礼物等等
 */
QString BaseWindow::processDanmakuVariants(QString msg, const LiveDanmaku& danmaku)
{
    QRegularExpressionMatch match;
    QRegularExpression re;

    // 去掉注释
    re = QRegularExpression("(?<!:)//.*?(?=\\n|$|\\\\n)");
    msg.replace(re, "");

    // 软换行符
    re = QRegularExpression("\\s*\\\\\\s*\\n\\s*");
    msg.replace(re, "");

    // 自动回复传入的变量
    re = QRegularExpression("%\\$(\\d+)%");
    while (msg.indexOf(re, 0, &match) > -1)
    {
        QString _var = match.captured(0);
        int i = match.captured(1).toInt();
        QString text = danmaku.getArgs(i);
        msg.replace(_var, text);
    }

    // 自定义变量
    for (auto it = customVariant.begin(); it != customVariant.end(); ++it)
    {
        msg.replace(it->first, it->second);
    }

    // 翻译
    for (auto it = variantTranslation.begin(); it != variantTranslation.end(); ++it)
    {
        msg.replace(it->first, it->second);
    }

    // 招呼变量（固定文字，随机内容）
    msg = processTimeVariants(msg);

    // 弹幕变量、环境变量（固定文字）
    re = QRegularExpression("%[\\w_]+?%");
    int matchPos = 0;
    while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
    {
        QString rpls = replaceDanmakuVariants(danmaku, match.captured(0));
        msg.replace(match.captured(0), rpls);
        matchPos = matchPos + rpls.length();
    }

    // 根据昵称替换为uid：倒找最近的弹幕、送礼
    re = QRegularExpression("%\\(([^(%)]+?)\\)%");
    while (msg.indexOf(re, 0, &match) > -1)
    {
        QString _var = match.captured(0);
        QString text = match.captured(1);
        msg.replace(_var, snum(unameToUid(text)));
    }

    // 替换额外数据
    re = QRegularExpression("%\\.([^%]+?)%");
    matchPos = 0;
    QJsonObject json = danmaku.extraJson;
    while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
    {
        QString rpls = replaceDanmakuExtras(json, match.captured(1));
        msg.replace(match.captured(0), rpls);
        matchPos += rpls.length();
    }

    bool find = true;
    while (find)
    {
        find = false;

        // 读取配置文件的变量
        re = QRegularExpression("%\\{([^(%(\\{|\\[|<))]*?)\\}%");
        while (msg.indexOf(re, 0, &match) > -1)
        {
            QString _var = match.captured(0);
            QString key = match.captured(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            QVariant var = heaps->value(key);
            msg.replace(_var, var.toString()); // 默认使用变量类型吧
            find = true;
        }

        // 进行数学计算的变量
        re = QRegularExpression("%\\[([^(%(\\{|\\[|<))]*?)\\]%");
        while (msg.indexOf(re, 0, &match) > -1)
        {
            QString _var = match.captured(0);
            QString text = match.captured(1);
            text = snum(calcIntExpression(text));
            msg.replace(_var, text); // 默认使用变量类型吧
            find = true;
        }

        // 函数替换
        re = QRegularExpression("%>(\\w+)\\s*\\(([^(%(\\{|\\[|<))]*?)\\)%");
        matchPos = 0;
        while ((matchPos = msg.indexOf(re, matchPos, &match)) > -1)
        {
            QString rpls = replaceDynamicVariants(match.captured(1), match.captured(2));
            msg.replace(match.captured(0), rpls);
            matchPos += rpls.length();
            find = true;
        }
    }

    // 一些用于替换特殊字符的东西，例如想办法避免无法显示 100% 这种

    return msg;
}

QString BaseWindow::replaceDanmakuVariants(const LiveDanmaku& danmaku, const QString &key) const
{
    // 用户昵称
    if (key == "%uname%" || key == "%username%" || key =="%nickname%")
        return danmaku.getNickname();

    // 用户昵称
    else if (key == "%uid%")
        return snum(danmaku.getUid());

    // 本地昵称+简化
    else if (key == "%ai_name%")
    {
        QString name = getLocalNickname(danmaku.getUid());
        if (name.isEmpty())
            name = nicknameSimplify(danmaku.getNickname());
        if (name.isEmpty())
            name = danmaku.getNickname();
        return name;
    }

    // 专属昵称
    else if (key == "%local_name%")
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = danmaku.getNickname();
        return local;
    }

    // 昵称简化
    else if (key == "%simple_name%")
    {
        return nicknameSimplify(danmaku.getNickname());
    }

    // 用户等级
    else if (key == "%level%")
        return snum(danmaku.getLevel());

    else if (key == "%text%")
        return danmaku.getText();

    // 进来次数
    else if (key == "%come_count%")
    {
        if (danmaku.getMsgType() == MSG_WELCOME)
            return snum(danmaku.getNumber());
        else
            return snum(danmakuCounts->value("come/"+snum(danmaku.getUid())).toInt());
    }

    // 上次进来
    else if (key == "%come_time%")
    {
        return snum(danmaku.getMsgType() == MSG_WELCOME
                                        ? danmaku.getPrevTimestamp()
                                        : danmakuCounts->value("comeTime/"+snum(danmaku.getUid())).toLongLong());
    }

    // 和现在的时间差
    else if (key == "%come_time_delta%")
    {
        qint64 prevTime = danmaku.getMsgType() == MSG_WELCOME
                ? danmaku.getPrevTimestamp()
                : danmakuCounts->value("comeTime/"+snum(danmaku.getUid())).toLongLong();
        return snum(QDateTime::currentSecsSinceEpoch() - prevTime);
    }

    // 本次送礼金瓜子
    else if (key == "%gift_gold%")
        return snum(danmaku.isGoldCoin() ? danmaku.getTotalCoin() : 0);

    // 本次送礼银瓜子
    else if (key == "%gift_silver%")
        return snum(danmaku.isGoldCoin() ? 0 : danmaku.getTotalCoin());

    // 本次送礼金瓜子+银瓜子（应该只有一个，但直接相加了）
    else if (key == "%gift_coin%")
        return snum(danmaku.getTotalCoin());

    // 是否是金瓜子礼物
    else if (key == "%coin_gold%")
        return danmaku.isGoldCoin() ? "1" : "0";

    // 本次送礼名字
    else if (key == "%gift_name%")
        return giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName();

    // 原始礼物名字
    else if (key == "%origin_gift_name%")
        return danmaku.getGiftName();

    // 本次送礼数量
    else if (key == "%gift_num%")
        return snum(danmaku.getNumber());

    else if (key == "%gift_multi_num%")
    {
        QString danwei = "个";
        QString giftName = danmaku.getGiftName();
        if (giftName.endsWith("船"))
            danwei = "艘";
        else if (giftName.endsWith("条"))
            danwei = "根";
        else if (giftName.endsWith("锦鲤"))
            danwei = "条";
        else if (giftName.endsWith("卡"))
            danwei = "张";
        else if (giftName.endsWith("灯"))
            danwei = "盏";
        else if (giftName.endsWith("阔落"))
            danwei = "瓶";
        else if (giftName.endsWith("花"))
            danwei = "朵";
        else if (giftName.endsWith("车"))
            danwei = "辆";
        else if (giftName.endsWith("情书"))
            danwei = "封";
        else if (giftName.endsWith("城"))
            danwei = "座";
        else if (giftName.endsWith("心心"))
            danwei = "颗";
        else if (giftName.endsWith("亿圆"))
            danwei = "枚";
        else if (giftName.endsWith("麦克风"))
            danwei = "支";
        return danmaku.getNumber() > 1 ? snum(danmaku.getNumber()) + danwei : "";
    }

    // 总共赠送金瓜子
    else if (key == "%total_gold%")
        return snum(danmakuCounts->value("gold/"+snum(danmaku.getUid())).toLongLong());

    // 总共赠送银瓜子
    else if (key == "%total_silver%")
        return snum(danmakuCounts->value("silver/"+snum(danmaku.getUid())).toLongLong());

    // 购买舰长
    else if (key == "%guard_buy%")
        return danmaku.is(MSG_GUARD_BUY) ? "1" : "0";

    else if (key == "%guard_count%")
        return snum(danmakuCounts->value("guard/" + snum(danmaku.getUid()), 0).toInt());

    // 0续费，1第一次上船，2重新上船
    else if (key == "%guard_first%" || key == "%first%")
        return snum(danmaku.getFirst());

    // 特别关注
    else if (key == "%special%")
        return snum(danmaku.getSpecial());

    // 粉丝牌房间
    else if (key == "%anchor_roomid%" || key == "%medal_roomid%" || key == "%anchor_room_id%" || key == "%medal_room_id%")
        return danmaku.getAnchorRoomid();

    // 粉丝牌名字
    else if (key == "%medal_name%")
        return danmaku.getMedalName();

    // 粉丝牌等级
    else if (key == "%medal_level%")
        return snum(danmaku.getMedalLevel());

    // 粉丝牌主播
    else if (key == "%medal_up%")
        return danmaku.getMedalUp();

    // 房管
    else if (key == "%admin%")
        return danmaku.isAdmin() ? "1" : (!upUid.isEmpty() && snum(danmaku.getUid())==upUid ? "1" : "0");

    // 舰长
    else if (key == "%guard%" || key == "%guard_level%")
        return snum(danmaku.getGuard());

    // 舰长名称
    else if (key == "%guard_name%" || key == "%guard_type%")
    {
        int guard = danmaku.getGuard();
        QString name = "";
        if (guard == 1)
            name = "总督";
        else if (guard == 2)
            name = "提督";
        else if (guard == 3)
            name = "舰长";
        return name;
    }

    // 房管或舰长
    else if (key == "%admin_or_guard%")
        return (danmaku.isGuard() || danmaku.isAdmin() || (!upUid.isEmpty() && snum(danmaku.getUid()) == upUid)) ? "1" : "0";

    // 是否是姥爷
    else if (key == "%vip%")
        return danmaku.isVip() ? "1" : "0";

    // 是否是年费姥爷
    else if (key == "%svip%")
        return danmaku.isSvip() ? "1" : "0";

    // 是否是正式会员
    else if (key == "%uidentity%")
        return danmaku.isUidentity() ? "1" : "0";

    // 是否有手机验证
    else if (key == "%iphone%")
        return danmaku.isIphone() ? "1" : "0";

    // 数量
    else if (key == "%number%")
        return snum(danmaku.getNumber());

    // 昵称长度
    else if (key == "%nickname_len%")
        return snum(danmaku.getNickname().length());

    // 礼物名字长度
    else if (key == "%giftname_len%")
        return snum((giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName()).length());

    // 昵称+礼物名字长度
    else if (key == "%name_sum_len%")
        return snum(danmaku.getNickname().length() + (giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName()).length());

    else if (key == "%ainame_sum_len%")
    {
        QString local = getLocalNickname(danmaku.getUid());
        if (local.isEmpty())
            local = nicknameSimplify(danmaku.getNickname());
        if (local.isEmpty())
            local = danmaku.getNickname();
        return snum(local.length() + (giftNames.contains(danmaku.getGiftId()) ? giftNames.value(danmaku.getGiftId()) : danmaku.getGiftName()).length());
    }

    // 是否新关注
    else if (key == "%new_attention%")
    {
        bool isInFans = false;
        qint64 uid = danmaku.getUid();
        foreach (FanBean fan, fansList)
            if (fan.mid == uid)
            {
                isInFans = true;
                break;
            }
        return isInFans ? "1" : "0";
    }

    // 是否是对面串门
    else if (key == "%pk_opposite%")
        return danmaku.isOpposite() ? "1" : "0";

    // 是否是己方串门回来
    else if (key == "%pk_view_return%")
        return danmaku.isViewReturn() ? "1" : "0";

    // 本次进来人次
    else if (key == "%today_come%")
        return snum(dailyCome);

    // 新人发言数量
    else if (key == "%today_newbie_msg%")
        return snum(dailyNewbieMsg);

    // 今天弹幕总数
    else if (key == "%today_danmaku%")
        return snum(dailyDanmaku);

    // 今天新增关注
    else if (key == "%today_fans%")
        return snum(dailyNewFans);

    // 当前粉丝数量111
    else if (key == "%fans_count%")
        return snum(dailyTotalFans);

    // 今天金瓜子总数
    else if (key == "%today_gold%")
        return snum(dailyGiftGold);

    // 今天银瓜子总数
    else if (key == "%today_silver%")
        return snum(dailyGiftSilver);

    // 今天是否有新舰长
    else if (key == "%today_guard%")
        return snum(dailyGuard);

    // 今日最高人气
    else if (key == "%today_max_ppl%")
        return snum(dailyMaxPopul);

    // 当前人气
    else if (key == "%popularity%")
        return snum(currentPopul);

    // 当前时间
    else if (key == "%time_hour%")
        return snum(QTime::currentTime().hour());
    else if (key == "%time_minute%")
        return snum(QTime::currentTime().minute());
    else if (key == "%time_second%")
        return snum(QTime::currentTime().second());
    else if (key == "%time_day%")
        return snum(QDate::currentDate().day());
    else if (key == "%time_month%")
        return snum(QDate::currentDate().month());
    else if (key == "%time_year%")
        return snum(QDate::currentDate().year());
    else if (key == "%time_day_week%")
        return snum(QDate::currentDate().dayOfWeek());
    else if (key == "%time_day_year%")
        return snum(QDate::currentDate().dayOfYear());
    else if (key == "%timestamp%")
        return snum(QDateTime::currentSecsSinceEpoch());
    else if (key == "%timestamp13%")
        return snum(QDateTime::currentMSecsSinceEpoch());

    // 大乱斗
    else if (key == "%pking%")
        return snum(pking ? 1 : 0);
    else if (key == "%pk_video%")
        return snum(pkVideo ? 1 : 0);
    else if (key == "%pk_room_id%")
        return pkRoomId;
    else if (key == "%pk_uid%")
        return pkUid;
    else if (key == "%pk_uname%")
        return pkUname;
    else if (key == "%pk_count%")
        return snum(pking && !pkRoomId.isEmpty() ? danmakuCounts->value("pk/" + pkRoomId, 0).toInt() : 0);
    else if (key == "%pk_touta_prob%")
    {
        int prob = 0;
        if (pking && !pkRoomId.isEmpty())
        {
            int totalCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt() - 1;
            int toutaCount = danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
            if (totalCount > 1)
                prob = toutaCount * 100 / totalCount;
        }
        return snum(prob);
    }

    else if (key == "%pk_my_votes%")
        return snum(myVotes);
    else if (key == "%pk_match_votes%")
        return snum(matchVotes);
    else if (key == "%pk_ending%")
        return snum(pkEnding ? 1 : 0);
    else if (key == "%pk_trans_gold%")
        return snum(goldTransPk);
    else if (key == "%pk_max_gold%")
        return snum(pkMaxGold);

    else if (key == "%pk_id%")
        return snum(pkId);

    // 房间属性
    else if (key == "%living%")
        return snum(liveStatus);
    else if (key == "%room_id%")
        return roomId;
    else if (key == "%room_name%")
        return roomTitle;
    else if (key == "%up_name%" || key == "%up_uname%")
        return upName;
    else if (key == "%up_uid%")
        return upUid;
    else if (key == "%my_uid%")
        return cookieUid;
    else if (key == "%my_uname%")
        return cookieUname;

    // 是主播
    else if (key == "%is_up%")
        return danmaku.getUid() == upUid.toLongLong() ? "1" : "0";
    // 是机器人
    else if (key == "%is_me%")
        return danmaku.getUid() == cookieUid.toLongLong() ? "1" : "0";
    // 戴房间勋章
    else if (key == "%is_room_medal%")
        return danmaku.getAnchorRoomid() == roomId ? "1" : "0";

    // 本地设置
    // 特别关心
    else if (key == "%care%")
        return careUsers.contains(danmaku.getUid()) ? "1" : "0";
    // 强提醒
    else if (key == "%strong_notify%")
        return strongNotifyUsers.contains(danmaku.getUid()) ? "1" : "0";
    // 是否被禁言
    else if (key == "%blocked%")
        return userBlockIds.contains(danmaku.getUid()) ? "1" : "0";
    // 不自动欢迎
    else if (key == "%not_welcome%")
        return notWelcomeUsers.contains(danmaku.getUid()) ? "1" : "0";
    // 不自动欢迎
    else if (key == "%not_reply%")
        return notReplyUsers.contains(danmaku.getUid()) ? "1" : "0";

    // 弹幕人气
    else if (key == "%danmu_popularity%")
        return snum(danmuPopulValue);

    // 游戏用户
    else if (key == "%in_game_users%")
        return gameUsers[0].contains(danmaku.getUid()) ? "1" : "0";
    else if (key == "%in_game_numbers%")
        return gameNumberLists[0].contains(danmaku.getUid()) ? "1" : "0";
    else if (key == "%in_game_texts%")
        return gameTextLists[0].contains(danmaku.getText()) ? "1" : "0";

    // 程序路径
    else if (key == "%app_path%")
        return dataPath;

    // cookie
    else if (key == "%csrf%")
        return csrf_token;

    // 工作状态
    else if (key == "%working%")
        return (shallAutoMsg() && (ui->autoSendWelcomeCheck->isChecked() || ui->autoSendGiftCheck->isChecked() || ui->autoSendAttentionCheck->isChecked())) ? "1" : "0";

    // 用户备注
    else if (key == "%umark%")
        return userMarks->value("base/" + snum(danmaku.getUid()), "").toString();

    // 正则播放的音乐
    else if (key == "%playing_song%")
    {
        QString name = "";
        if (musicWindow)
        {
            Song song = musicWindow->getPlayingSong();
            if (song.isValid())
                name = song.name;
        }
        return name;
    }
    // 点歌的用户
    else if (key == "%song_order_uname%")
    {
        QString name = "";
        if (musicWindow)
        {
            Song song = musicWindow->getPlayingSong();
            if (song.isValid())
                name = song.addBy;
        }
        return name;
    }
    // 点歌队列数量
    else if (key == "%order_song_count%")
    {
        QString text = "0";
        if (musicWindow)
        {
            text = snum(musicWindow->getOrderSongs().size());
        }
        return text;
    }
    else if (key == "%random100%")
    {
        return snum(qrand() % 100 + 1);
    }
    else
        return "";
}

/**
 * 额外数据（JSON）替换
 */
QString BaseWindow::replaceDanmakuExtras(const QJsonObject &json, const QString& key_seq) const
{
    QStringList keyTree = key_seq.split(".");
    if (keyTree.size() == 0)
        return "";

    QJsonValue obj = json.value(keyTree.takeFirst());
    while (keyTree.size())
    {
        QString key = keyTree.takeFirst();
        if (key.isEmpty())
        {
            obj = QJsonValue();
            break;
        }
        else if (obj.isObject())
        {
            obj = obj.toObject().value(key);
        }
        else if (obj.isArray())
        {
            int index = key.toInt();
            QJsonArray array = obj.toArray();
            if (index >= 0 && index < array.size())
                obj = array.at(index);
            else
            {
                obj = QJsonValue();
                break;
            }
        }
        else
        {
            obj = QJsonValue();
            break;
        }
    }

    if (obj.isNull() || obj.isUndefined())
        return "";
    if (obj.isString())
        return obj.toString();
    if (obj.isBool())
        return obj.toBool(false) ? "1" : "0";
    if (obj.isDouble())
    {
        double val = obj.toDouble();
        if (qAbs(val - qint64(val)) < 1e-6) // 是整数类型的
            return QString::number(qint64(val));
        return QString::number(val);
    }
    if (obj.isObject() || obj.isArray()) // 不支持转换的类型
        return "";
    return "";
}

/**
 * 函数替换
 */
QString BaseWindow::replaceDynamicVariants(const QString &funcName, const QString &args)
{
    QRegularExpressionMatch match;
    QStringList argList = args.split(QRegExp("\\s*,\\s*"));
    auto errorArg = [=](QString tip){
        localNotify("函数%>"+funcName+"()%参数错误: " + tip);
        return "";
    };

    // 替换时间
    if (funcName == "simpleName")
    {
        return nicknameSimplify(args);
    }
    else if (funcName == "simpleNum")
    {
        return numberSimplify(args.toInt());
    }
    else if (funcName == "time")
    {
        return QDateTime::currentDateTime().toString(args);
    }
    else if (funcName == "unameToUid")
    {
        return snum(unameToUid(args));
    }
    else if (funcName == "inGameUsers")
    {
        int ch = 0;
        qint64 id = 0;
        if (argList.size() == 1)
            id = argList.first().toLongLong();
        else if (argList.size() >= 2)
        {
            ch = argList.at(0).toInt();
            id = argList.at(1).toLongLong();
        }
        if (ch < 0 || ch >= CHANNEL_COUNT)
            ch = 0;
        return gameUsers[ch].contains(id) ? "1" : "0";
    }
    else if (funcName == "inGameNumbers")
    {
        int ch = 0;
        qint64 id = 0;
        if (argList.size() == 1)
            id = argList.first().toLongLong();
        else if (argList.size() >= 2)
        {
            ch = argList.at(0).toInt();
            id = argList.at(1).toLongLong();
        }
        if (ch < 0 || ch >= CHANNEL_COUNT)
            ch = 0;
        return gameNumberLists[ch].contains(id) ? "1" : "0";
    }
    else if (funcName == "inGameTexts")
    {
        int ch = 0;
        QString text;
        if (argList.size() == 1)
            text = argList.first();
        else if (argList.size() >= 2)
        {
            ch = argList.at(0).toInt();
            text = argList.at(1);
        }
        if (ch < 0 || ch >= CHANNEL_COUNT)
            ch = 0;
        return gameTextLists[ch].contains(text) ? "1" : "0";
    }
    else if (funcName == "strlen")
    {
        return snum(args.size());
    }
    else if (funcName == "trim")
    {
        return args.trimmed();
    }
    else if (funcName == "substr")
    {
        if (argList.size() < 2)
            return errorArg("字符串, 起始位置, 长度");

        QString text = argList.at(0);
        int left = 0, len = text.length();
        if (argList.size() >= 2)
            left = argList.at(1).toInt();
        if (argList.size() >= 3)
            len = argList.at(2).toInt();
        if (left < 0)
            left = 0;
        else if (left > text.length())
            left = text.length();
        return text.mid(left, len);
    }
    else if (funcName == "replace")
    {
        if (argList.size() < 3)
            return errorArg("字符串, 原文本, 新文本");

        QString text = argList.at(0);
        return text.replace(argList.at(1), argList.at(2));
    }
    else if (funcName == "replaceReg" || funcName == "regReplace")
    {
        if (argList.size() < 3)
            return errorArg("字符串, 原正则, 新文本");

        QString text = argList.at(0);
        return text.replace(QRegularExpression(argList.at(1)), argList.at(2));
    }
    else if (funcName == "reg")
    {
        if (argList.size() < 2)
            return errorArg("字符串, 正则表达式");
        int index = args.lastIndexOf(",");
        if (index == -1) // 不应该没找到的
            return "";
        QString full = args.left(index);
        QString reg = args.right(args.length() - index - 1).trimmed();

        QRegularExpressionMatch match;
        if (full.indexOf(QRegularExpression(reg), 0, &match) == -1)
            return "";
        return match.captured(0);
    }
    else if (funcName == "inputText")
    {
        QString label = argList.size() ? argList.first() : "";
        QString def = argList.size() >= 2 ? argList.at(1) : "";
        QString rst = QInputDialog::getText(this, QApplication::applicationName(), label, QLineEdit::Normal, def);
        return rst;
    }
    else if (funcName == "getValue")
    {
        if (argList.size() < 1 || argList.first().trimmed().isEmpty())
            return errorArg("键, 默认值");
        QString key = argList.at(0);
        QString def = argList.size() >= 2 ? argList.at(1) : "";
        if (!key.contains("/"))
            key = "heaps/" + key;
        return heaps->value(key, def).toString();
    }
    else if (funcName == "random")
    {
        if (argList.size() < 1 || argList.first().trimmed().isEmpty())
            return errorArg("最小值，最大值");
        int min = 0, max = 100;
        if (argList.size() == 1)
            max = argList.at(0).toInt();
        else
        {
            min = argList.at(0).toInt();
            max = argList.at(1).toInt();
        }
        if (max < min)
        {
            int t = min;
            min = max;
            max = t;
        }
        return snum(qrand() % (max-min+1) + min);
    }
    return "";
}

/**
 * 处理条件变量
 * [exp1, exp2]...
 * 要根据时间戳、字符串
 * @return 如果返回空字符串，则不符合；否则返回去掉表达式后的正文
 */
QString BaseWindow::processMsgHeaderConditions(QString msg) const
{
    QRegularExpression re("^\\s*\\[(.*?)\\]\\s*");
    QRegularExpressionMatch match;
    if (msg.indexOf(re, 0, &match) == -1) // 没有检测到表达式
        return msg;

    QString totalExp = match.capturedTexts().first(); // 整个表达式，带括号
    QString exprs = match.capturedTexts().at(1);

    if (!processVariantConditions(exprs))
        return "";
    return msg.right(msg.length() - totalExp.length());
}

/**
 * 判断逻辑条件是否成立
 * exp1, exp2; exp3
 */
bool BaseWindow::processVariantConditions(QString exprs) const
{
    QStringList orExps = exprs.split(QRegularExpression("(;|\\|\\|)"), QString::SkipEmptyParts);
    bool isTrue = false;
    QRegularExpression compRe("^\\s*([^<>=!~]*?)\\s*([<>=!~]{1,2})\\s*([^<>=!~]*?)\\s*$");
    QRegularExpression intRe("^[\\d\\+\\-\\*\\/%]+$");
    QRegularExpressionMatch match;
    foreach (QString orExp, orExps)
    {
        isTrue = true;
        QStringList andExps = orExp.split(QRegularExpression("(,|&&)"), QString::SkipEmptyParts);
        CALC_DEB << "表达式or内：" << andExps;
        foreach (QString exp, andExps)
        {
            CALC_DEB << "表达式and内：" << exp;
            exp = exp.trimmed();
            if (exp.indexOf(compRe, 0, &match) == -1         // 非比较
                    || (match.captured(1).isEmpty() && match.captured(2) == "!"))    // 取反类型
            {
                bool notTrue = exp.startsWith("!"); // 与否取反
                if (notTrue) // 取反……
                {
                    exp = exp.right(exp.length() - 1);
                }
                if (exp.isEmpty() || exp == "0" || exp.toLower() == "false") // false
                {
                    if (!notTrue)
                    {
                        isTrue = false;
                        break;
                    }
                    else // 取反
                    {
                        isTrue = true;
                        break;
                    }
                }
                else // true
                {
                    if (notTrue)
                    {
                        isTrue = false;
                        break;
                    }
                }
                continue;
            }

            // 比较类型
            QStringList caps = match.capturedTexts();
            QString s1 = caps.at(1);
            QString op = caps.at(2);
            QString s2 = caps.at(3);
            CALC_DEB << "比较：" << s1 << op << s2;
            if (s1.indexOf(intRe) > -1 && s2.indexOf(intRe) > -1) // 都是整数
            {
                qint64 i1 = calcIntExpression(s1);
                qint64 i2 = calcIntExpression(s2);
                CALC_DEB << "比较整数" << i1 << op << i2;
                if (!isConditionTrue<qint64>(i1, i2, op))
                {
                    isTrue = false;
                    break;
                }
            }
            else/* if (s1.startsWith("\"") || s1.endsWith("\"") || s1.startsWith("'") || s1.endsWith("'")
                    || s2.startsWith("\"") || s2.startsWith("\"") || s2.startsWith("'") || s2.startsWith("'")) // 都是字符串*/
            {
                auto removeQuote = [=](QString s) -> QString{
                    if (s.startsWith("\"") && s.endsWith("\""))
                        return s.mid(1, s.length()-2);
                    if (s.startsWith("'") && s.endsWith("'"))
                        return s.mid(1, s.length()-2);
                    return s;
                };
                s1 = removeQuote(s1);
                s2 = removeQuote(s2);
                CALC_DEB << "比较字符串" << s1 << op << s2;
                if (op == "~")
                {
                    if (!s1.contains(QRegularExpression(s2)))
                    {
                        isTrue = false;
                        break;
                    }
                }
                else if (!isConditionTrue<QString>(s1, s2, op))
                {
                    isTrue = false;
                    break;
                }
            }
            /*else
            {
                qCritical() << "error: 无法比较的表达式:" << match.capturedTexts().first();
                qCritical() << "    原始语句：" << msg;
            }*/
        }
        if (isTrue)
            break;
    }
    return isTrue;
}

/**
 * 计算纯int、运算符组成的表达式
 */
qint64 BaseWindow::calcIntExpression(QString exp) const
{
    exp = exp.replace(QRegularExpression("\\s*"), ""); // 去掉所有空白
    QRegularExpression opRe("[\\+\\-\\*/%]");

    // 获取所有整型数值
    QStringList valss = exp.split(opRe); // 如果是-开头，那么会当做 0-x
    if (valss.size() == 0)
        return 0;
    QList<qint64> vals;
    foreach (QString val, valss)
    {
        vals << val.toLongLong();
    }

    // 获取所有运算符
    QStringList ops;
    QRegularExpressionMatchIterator i = opRe.globalMatch(exp);
    while (i.hasNext())
    {
        ops << i.next().captured(0);
    }
    if (valss.size() != ops.size() + 1)
    {
        qCritical() << "错误的表达式：" << valss << ops << exp;
        return 0;
    }

    // 入栈：* / %
    for (int i = 0; i < ops.size(); i++)
    {
        // op[i] 操作 vals[i] x vals[i+1]
        if (ops[i] == "*")
        {
            vals[i] *= vals[i+1];
        }
        else if (ops[i] == "/")
        {
            qDebug() << "除法" << ops << vals;
            if (vals[i+1] == 0)
            {
                qWarning() << "!!!被除数是0 ：" << exp;
                vals[i+1] = 1;
            }
            vals[i] /= vals[i+1];
        }
        else if (ops[i] == "%")
        {
            if (vals[i+1] == 0)
            {
                qWarning() << "!!!被模数是0 ：" << exp;
                vals[i+1] = 1;
            }
            vals[i] %= vals[i+1];
        }
        else
            continue;
        vals.removeAt(i+1);
        ops.removeAt(i);
        i--;
    }

    // 顺序计算：+ -
    qint64 val = vals.first();
    for (int i = 0; i < ops.size(); i++)
    {
        if (ops[i] == "-")
            val -= vals[i+1];
        else if (ops[i] == "+")
            val += vals[i+1];
    }

    return val;
}

qint64 BaseWindow::unameToUid(QString text)
{
    // 查找弹幕和送礼
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;

        QString nick = danmaku.getNickname();
        if (nick.contains(text))
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UNAME", danmaku);
            return danmaku.getUid();
        }
    }

    // 查找专属昵称
    QSet<qint64> hadMatches;
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;
        qint64 uid = danmaku.getUid();
        if (hadMatches.contains(uid) || !localNicknames.contains(uid))
            continue;
        QString nick = localNicknames.value(uid);
        if (nick.contains(text))
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UNAME", danmaku);
            return danmaku.getUid();
        }
        hadMatches.insert(uid);
    }

    localNotify("[未找到用户：" + text + "]");
    triggerCmdEvent("NOT_FIND_USER_BY_UNAME", LiveDanmaku(text));
    return 0;
}

QString BaseWindow::uidToName(qint64 uid)
{
    // 查找弹幕和送礼
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;

        if (danmaku.getUid() == uid)
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UID", danmaku);
            return danmaku.getNickname();
        }
    }

    // 查找专属昵称
    QSet<qint64> hadMatches;
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        const LiveDanmaku danmaku = roomDanmakus.at(i);
        if (!danmaku.is(MSG_DANMAKU) && !danmaku.is(MSG_GIFT))
            continue;
        if (danmaku.getUid() == uid)
        {
            // 就是这个人
            triggerCmdEvent("FIND_USER_BY_UID", danmaku);
            return danmaku.getNickname();
        }
        hadMatches.insert(uid);
    }

    localNotify("[未找到用户：" + snum(uid) + "]");
    triggerCmdEvent("NOT_FIND_USER_BY_UID", LiveDanmaku(snum(uid)));
    return "";
}

/**
 * 一个智能的用户昵称转简单称呼
 */
QString BaseWindow::nicknameSimplify(QString nickname) const
{
    QString simp = nickname;

    // 没有取名字的，就不需要欢迎了
    /*QRegularExpression defaultRe("^([bB]ili_\\d+|\\d+_[bB]ili)$");
    if (simp.indexOf(defaultRe) > -1)
    {
        return "";
    }*/

    // 去掉前缀后缀
    QStringList special{"~", "丶", "°", "゛", "-", "_", "ヽ"};
    QStringList starts{"我叫", "我是", "我就是", "可是", "一只", "是个", "是", "原来", "但是", "但", "在下", "做", "隔壁", "的"};
    QStringList ends{"啊", "呢", "呀", "哦", "呐", "巨凶", "吧", "呦", "诶", "哦", "噢", "吖", "Official"};
    starts += special;
    ends += special;
    for (int i = 0; i < starts.size(); i++)
    {
        QString start = starts.at(i);
        if (simp.startsWith(start))
        {
            simp.remove(0, start.length());
            i = 0; // 从头开始
        }
    }
    for (int i = 0; i < ends.size(); i++)
    {
        QString end = ends.at(i);
        if (simp.endsWith(end))
        {
            simp.remove(simp.length() - end.length(), end.length());
            i = 0; // 从头开始
        }
    }

    // 默认名字
    QRegularExpression defRe("(\\d+)_[Bb]ili");
    QRegularExpressionMatch match;
    if (simp.indexOf(defRe, 0, &match) > -1)
    {
        simp = match.capturedTexts().at(1);
    }

    // 去掉首尾数字
    QRegularExpression snumRe("^\\d+(\\D+)\\d*$");
    if (simp.indexOf(snumRe, 0, &match) > -1
            && match.captured(1).indexOf(QRegExp("^[的是]")) == -1)
    {
        simp = match.capturedTexts().at(1);
    }
    snumRe = QRegularExpression("^(\\D+)\\d+$");
    if (simp.indexOf(snumRe, 0, &match) > -1
            && match.captured(1) != "bili_"
            && match.captured(1).indexOf(QRegExp("^[的是]")) == -1)
    {
        simp = match.capturedTexts().at(1);
    }

    // xxx的xxx
    QRegularExpression deRe("^(.{2,})[的の]([\\w\\d_\\-\u4e00-\u9fa5]{2,})$");
    if (simp.indexOf(deRe, 0, &match) > -1 && match.capturedTexts().at(1).length() <= match.capturedTexts().at(2).length()*2)
    {
        QRegularExpression blankL("(我$)"), blankR("(名字|^确|最)");
        if (match.captured(1).indexOf(blankL) == -1
                && match.captured(2).indexOf(blankR) == -1) // 不包含黑名单
            simp = match.capturedTexts().at(2);
    }

    // 一大串 中文enen
    // 注：日语正则 [\u0800-\u4e00] ，实测“一”也会算在里面……？
    QRegularExpression ceRe("([\u4e00-\u9fa5]{2,})([-\\w\\d_\u0800-\u4dff]+)$");
    if (simp.indexOf(ceRe, 0, &match) > -1 && match.capturedTexts().at(1).length()*3 >= match.capturedTexts().at(2).length())
    {
        QString tmp = match.capturedTexts().at(1);
        if (!QString("的之の是叫有为奶在去着最很").contains(tmp.right(1)))
        {
            simp = tmp;
        }
    }
    // enen中文
    ceRe = QRegularExpression("^([-\\w\\d_\u0800-\u4dff]+)([\u4e00-\u9fa5]{2,})$");
    if (simp.indexOf(ceRe, 0, &match) > -1 && match.capturedTexts().at(1).length() <= match.capturedTexts().at(2).length()*3)
    {
        QString tmp = match.capturedTexts().at(2);
        if (!QString("的之の是叫有为奶在去着最").contains(tmp.right(1)))
        {
            simp = tmp;
        }
    }

    QStringList extraExp{"^这个(.+)不太.+$", "^(.{3,})今天.+$", "最.+的(.{2,})$",
                         "^.+(?:我就是|叫我)(.+)$", "^.*还.+就(.{2})$",
                         "^(.{2,})(.)不\\2.*",
                         "^(.{2}?)(不|有点|才是|敲|很|能有|想|从不|才不|跟你).+",
                        "^(.{2,})-(.{2,})$"};
    for (int i = 0; i < extraExp.size(); i++)
    {
        QRegularExpression re(extraExp.at(i));
        if (simp.indexOf(re, 0, &match) > -1)
        {
            simp = match.capturedTexts().at(1);
            break;
        }
    }

    // 特殊字符
    simp = simp.replace(QRegularExpression("_|丨|丶|灬|ミ|丷|I"), "");

    // xxx哥哥
    QRegularExpression gegeRe("^(.{2,}?)(大|小|老)?(公|婆|鸽鸽|哥哥|爸爸|爷爷|奶奶|妈妈|朋友|盆友|魔王|可爱|参上)$");
    if (simp.indexOf(gegeRe, 0, &match) > -1)
    {
        QString tmp = match.capturedTexts().at(1);
        simp = tmp;
    }

    // AAAA
    QRegularExpression aaRe("(.)\\1{2,}");
    if (simp.indexOf(aaRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1%1").arg(ch));
    }

    // ABAB
    QRegularExpression abRe = QRegularExpression("(.{2,})\\1{1,}");
    if (simp.indexOf(abRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1").arg(ch));
    }

    // Name1Name2
    QRegularExpression sunameRe = QRegularExpression("^[A-Z]*?([A-Z][a-z0-9]+)[-_A-Z0-9]([\\w_\\-])*$");
    if (simp.indexOf(sunameRe, 0, &match) > -1)
    {
        QString ch = match.capturedTexts().at(1);
        QString all = match.capturedTexts().at(0);
        simp = simp.replace(all, QString("%1").arg(ch));
    }

    // 一长串数字
    QRegularExpression numRe("(\\d{3})\\d{3,}");
    if (simp.indexOf(numRe, 0, &match) > -1)
    {
        simp = simp.replace(match.captured(0), match.captured(1) + "…");
    }

    // 一长串英文
    QRegularExpression wRe("(\\w{5})\\w{3,}");
    if (simp.indexOf(wRe, 0, &match) > -1)
    {
        simp = simp.replace(match.captured(0), match.captured(1) + "…");
    }

    if (simp.isEmpty())
        return nickname;
    return simp;
}

QString BaseWindow::numberSimplify(int number) const
{
    if (number < 10000)
        return QString::number(number);
    number = (number + 5000) / 10000;
    return QString::number(number) + "万";
}

QString BaseWindow::msgToShort(QString msg) const
{
    if (msg.startsWith(">") || msg.length() <= danmuLongest)
        return msg;
    if (msg.contains(" "))
    {
        msg = msg.replace(" ", "");
        if (msg.length() <= 20)
            return msg;
    }
    if (msg.contains("“"))
    {
        msg = msg.replace("“", "");
        msg = msg.replace("”", "");
        if (msg.length() <= 20)
            return msg;
    }
    return msg;
}

double BaseWindow::getPaletteBgProg() const
{
    return paletteProg;
}

void BaseWindow::setPaletteBgProg(double x)
{
    this->paletteProg = x;
}

void BaseWindow::startSaveDanmakuToFile()
{
    if (danmuLogFile)
        finishSaveDanmuToFile();

    QDir dir;
    dir.mkdir(dataPath+"danmaku_histories");
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");

    qDebug() << "开启弹幕记录：" << dataPath+"danmaku_histories/" + roomId + "_" + date + ".log";
    danmuLogFile = new QFile(dataPath+"danmaku_histories/" + roomId + "_" + date + ".log");
    danmuLogFile->open(QIODevice::WriteOnly | QIODevice::Append);
    danmuLogStream = new QTextStream(danmuLogFile);
}

void BaseWindow::finishSaveDanmuToFile()
{
    if (!danmuLogFile)
        return ;

    delete danmuLogStream;
    danmuLogFile->close();
    danmuLogFile->deleteLater();
    danmuLogFile = nullptr;
    danmuLogStream = nullptr;
}

void BaseWindow::startCalculateDailyData()
{
    if (dailySettings)
    {
        saveCalculateDailyData();
        dailySettings->deleteLater();
    }

    QDir dir;
    dir.mkdir(dataPath+"live_daily");
    QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    dailySettings = new QSettings(dataPath+"live_daily/" + roomId + "_" + date + ".ini", QSettings::Format::IniFormat);

    dailyCome = dailySettings->value("come", 0).toInt();
    dailyPeopleNum = dailySettings->value("people_num", 0).toInt();
    dailyDanmaku = dailySettings->value("danmaku", 0).toInt();
    dailyNewbieMsg = dailySettings->value("newbie_msg", 0).toInt();
    dailyNewFans = dailySettings->value("new_fans", 0).toInt();
    dailyTotalFans = dailySettings->value("total_fans", 0).toInt();
    dailyGiftSilver = dailySettings->value("gift_silver", 0).toInt();
    dailyGiftGold = dailySettings->value("gift_gold", 0).toInt();
    dailyGuard = dailySettings->value("guard", 0).toInt();
    dailyMaxPopul = dailySettings->value("max_popularity", 0).toInt();
    dailyAvePopul = 0;
    if (currentGuards.size())
        dailySettings->setValue("guard_count", currentGuards.size());
    else
        updateExistGuards(0);
}

void BaseWindow::saveCalculateDailyData()
{
    if (dailySettings)
    {
        dailySettings->setValue("come", dailyCome);
        dailySettings->setValue("people_num", qMax(dailySettings->value("people_num").toInt(), userComeTimes.size()));
        dailySettings->setValue("danmaku", dailyDanmaku);
        dailySettings->setValue("newbie_msg", dailyNewbieMsg);
        dailySettings->setValue("new_fans", dailyNewFans);
        dailySettings->setValue("total_fans", currentFans);
        dailySettings->setValue("gift_silver", dailyGiftSilver);
        dailySettings->setValue("gift_gold", dailyGiftGold);
        dailySettings->setValue("guard", dailyGuard);
        if (currentGuards.size())
            dailySettings->setValue("guard_count", currentGuards.size());
    }
}

void BaseWindow::saveTouta()
{
    settings->setValue("pk/toutaCount", toutaCount);
    settings->setValue("pk/chiguaCount", chiguaCount);
    ui->pkAutoMelonCheck->setToolTip(QString("偷塔次数：%1\n吃瓜数量：%2").arg(toutaCount).arg(chiguaCount));
}

void BaseWindow::startLiveRecord()
{
    finishLiveRecord();
    if (roomId.isEmpty())
        return ;

    getRoomLiveVideoUrl([=](QString url){
        recordUrl = "";

        qDebug() << "开始录播：" << url;
        QNetworkAccessManager manager;
        QNetworkRequest* request = new QNetworkRequest(QUrl(url));
        QEventLoop* loop = new QEventLoop();
        QNetworkReply *reply = manager.get(*request);

        QObject::connect(reply, SIGNAL(finished()), loop, SLOT(quit())); //请求结束并下载完成后，退出子事件循环
        connect(reply, &QNetworkReply::downloadProgress, this, [=](qint64 bytesReceived, qint64 bytesTotal){
            if (bytesReceived > 0)
            {
                qDebug() << "原始地址可直接下载";
                recordUrl = url;
                loop->quit();
                reply->deleteLater();
            }
        });
        loop->exec(); //开启子事件循环
        loop->deleteLater();

        // B站下载会有302重定向的，需要获取headers里面的Location值
        if (recordUrl.isEmpty())
        {
            recordUrl = reply->rawHeader("location");
            /*auto headers = reply->rawHeaderPairs();
            for (int i = 0; i < headers.size(); i++)
                if (headers.at(i).first.toLower() == "location")
                {
                    recordUrl = headers.at(i).second;
                    qDebug() << "重定向下载地址：" << recordUrl;
                    break;
                }*/
            reply->deleteLater();
        }
        delete request;
        if (recordUrl.isEmpty())
        {
            qDebug() << "无法获取下载地址";
            return ;
        }

        // 开始下载文件
        startRecordUrl(recordUrl);
    });
}

void BaseWindow::startRecordUrl(QString url)
{
    QDir dir(dataPath);
    dir.mkpath("record");
    dir.cd("record");
    QString path = QFileInfo(dir.absoluteFilePath(
                                 roomId + "_" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss") + ".mp4"))
            .absoluteFilePath();

    ui->recordCheck->setText("录制中...");
    recordTimer->start();
    startRecordTime = QDateTime::currentMSecsSinceEpoch();
    recordLoop = new QEventLoop;
    QNetworkAccessManager manager;
    QNetworkRequest* request = new QNetworkRequest(QUrl(url));
    QNetworkReply *reply = manager.get(*request);
    QObject::connect(reply, SIGNAL(finished()), recordLoop, SLOT(quit())); //请求结束并下载完成后，退出子事件循环
    recordLoop->exec(); //开启子事件循环
    recordLoop->deleteLater();
    recordLoop = nullptr;
    qDebug() << "录播结束：" << path;

    QFile file(path);
    if (!file.open(QFile::WriteOnly))
    {
        qCritical() << "写入文件失败" << path;
        reply->deleteLater();
        return ;
    }
    QByteArray data = reply->readAll();
    if (!data.isEmpty())
    {
        qint64 write_bytes = file.write(data);
        file.flush();
        if (write_bytes != data.size())
            qCritical() << "写入文件大小错误" << write_bytes << "/" << data.size();
    }

    reply->deleteLater();
    delete request;
    startRecordTime = 0;
    ui->recordCheck->setText("录播");
    recordTimer->stop();

    // 可能是超时结束了，重新下载
    if (ui->recordCheck->isChecked() && isLiving())
    {
        startLiveRecord();
    }
}

void BaseWindow::finishLiveRecord()
{
    if (!recordLoop)
        return ;
    qDebug() << "结束录播";
    recordLoop->quit();
    recordLoop = nullptr;
}

void BaseWindow::processRemoteCmd(QString msg, bool response)
{
    if (!remoteControl)
        return ;

    if (msg == "关闭功能")
    {
        processRemoteCmd("关闭欢迎", false);
        processRemoteCmd("关闭关注答谢", false);
        processRemoteCmd("关闭送礼答谢", false);
        processRemoteCmd("关闭禁言", false);
        if (response)
            sendNotifyMsg(">已暂停自动弹幕");
    }
    else if (msg == "开启功能")
    {
        processRemoteCmd("开启欢迎", false);
        processRemoteCmd("开启关注答谢", false);
        processRemoteCmd("开启送礼答谢", false);
        processRemoteCmd("开启禁言", false);
        if (response)
            sendNotifyMsg(">已开启自动弹幕");
    }
    else if (msg == "关闭机器人")
    {
        socket->abort();
        connectServerTimer->stop();
    }
    else if (msg == "关闭欢迎")
    {
        ui->autoSendWelcomeCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动欢迎");
    }
    else if (msg == "开启欢迎")
    {
        ui->autoSendWelcomeCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动欢迎");
    }
    else if (msg == "关闭关注答谢")
    {
        ui->autoSendAttentionCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动答谢关注");
    }
    else if (msg == "开启关注答谢")
    {
        ui->autoSendAttentionCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动答谢关注");
    }
    else if (msg == "关闭送礼答谢")
    {
        ui->autoSendGiftCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动答谢送礼");
    }
    else if (msg == "开启送礼答谢")
    {
        ui->autoSendGiftCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动答谢送礼");
    }
    else if (msg == "关闭禁言")
    {
        ui->autoBlockNewbieCheck->setChecked(false);
        on_autoBlockNewbieCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停新人关键词自动禁言");
    }
    else if (msg == "开启禁言")
    {
        ui->autoBlockNewbieCheck->setChecked(true);
        on_autoBlockNewbieCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启新人关键词自动禁言");
    }
    else if (msg == "关闭偷塔")
    {
        ui->pkAutoMelonCheck->setChecked(false);
        on_pkAutoMelonCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停自动偷塔");
    }
    else if (msg == "开启偷塔")
    {
        ui->pkAutoMelonCheck->setChecked(true);
        on_pkAutoMelonCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启自动偷塔");
    }
    else if (msg == "关闭点歌")
    {
        ui->DiangeAutoCopyCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已暂停自动点歌");
    }
    else if (msg == "开启点歌")
    {
        ui->DiangeAutoCopyCheck->setChecked(true);
        if (response)
            sendNotifyMsg(">已开启自动点歌");
    }
    else if (msg == "关闭点歌回复")
    {
        ui->diangeReplyCheck->setChecked(false);
        on_diangeReplyCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停自动点歌回复");
    }
    else if (msg == "开启点歌回复")
    {
        ui->diangeReplyCheck->setChecked(true);
        on_diangeReplyCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启自动点歌回复");
    }
    else if (msg == "关闭自动连接")
    {
        ui->timerConnectServerCheck->setChecked(false);
        on_timerConnectServerCheck_clicked();
        if (response)
            sendNotifyMsg(">已暂停自动连接");
    }
    else if (msg == "开启自动连接")
    {
        ui->timerConnectServerCheck->setChecked(true);
        on_timerConnectServerCheck_clicked();
        if (response)
            sendNotifyMsg(">已开启自动连接");
    }
    else if (msg == "关闭定时任务")
    {
        for (int i = 0; i < ui->taskListWidget->count(); i++)
        {
            QListWidgetItem* item = ui->taskListWidget->item(i);
            auto widget = ui->taskListWidget->itemWidget(item);
            auto tw = static_cast<TaskWidget*>(widget);
            tw->check->setChecked(false);
        }
        saveTaskList();
        if (response)
            sendNotifyMsg(">已关闭定时任务");
    }
    else if (msg == "开启定时任务")
    {
        for (int i = 0; i < ui->taskListWidget->count(); i++)
        {
            QListWidgetItem* item = ui->taskListWidget->item(i);
            auto widget = ui->taskListWidget->itemWidget(item);
            auto tw = static_cast<TaskWidget*>(widget);
            tw->check->setChecked(true);
        }
        saveTaskList();
        if (response)
            sendNotifyMsg(">已开启定时任务");
    }
    else if (msg == "开启录播")
    {
        startLiveRecord();
        if (response)
            sendNotifyMsg(">已开启录播");
    }
    else if (msg == "关闭录播")
    {
        finishLiveRecord();
        if (response)
            sendNotifyMsg(">已关闭录播");
    }
    else if (ui->enableBlockCheck->isChecked() && msg == "撤销禁言")
    {
        if (!blockedQueue.size())
        {
            sendNotifyMsg(">没有可撤销的禁言用户");
            return ;
        }

        LiveDanmaku danmaku = blockedQueue.takeLast();
        delBlockUser(danmaku.getUid());
        if (eternalBlockUsers.contains(EternalBlockUser(danmaku.getUid(), roomId.toLongLong())))
        {
            eternalBlockUsers.removeOne(EternalBlockUser(danmaku.getUid(), roomId.toLongLong()));
            saveEternalBlockUsers();
        }
        if (response)
            sendNotifyMsg(">已解除禁言：" + danmaku.getNickname());
    }
    else if (ui->enableBlockCheck->isChecked() && msg.startsWith("禁言 "))
    {
        QRegularExpression re("^禁言\\s*(\\S+)\\s*(\\d+)?$");
        QRegularExpressionMatch match;
        if (msg.indexOf(re, 0, &match) == -1)
            return ;
        QString nickname = match.captured(1);
        QString hours = match.captured(2);
        int hour = ui->autoBlockTimeSpin->value();
        if (!hours.isEmpty())
            hour = hours.toInt();
        for (int i = roomDanmakus.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = roomDanmakus.at(i);
            if (!danmaku.is(MSG_DANMAKU))
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                // 是这个人了，判断是不是房管
                if (snum(danmaku.getUid()) == upUid)
                {
                    localNotify("无法禁言主播");
                }
                else if (snum(danmaku.getUid()) == cookieUid)
                {
                    localNotify("无法禁言自己");
                }
                else if (danmaku.isAdmin())
                {
                    localNotify("无法禁言房管");
                }
                else
                {
                    addBlockUser(danmaku.getUid(), hour);
                    if (!hasEvent("REMOTE_BLOCK_OVERRIDE"))
                        sendNotifyMsg(">已禁言：" + nick, true);
                }
                triggerCmdEvent("REMOTE_BLOCK", danmaku);
                triggerCmdEvent("REMOTE_BLOCK_OVERRIDE", danmaku);
                return ;
            }
        }
    }
    else if (ui->enableBlockCheck->isChecked() &&
             (msg.startsWith("解禁 ") || msg.startsWith("解除禁言 ") || msg.startsWith("取消禁言 ")))
    {
        QRegularExpression re("^(?:解禁|解除禁言|取消禁言)\\s*(.+)\\s*$");
        QRegularExpressionMatch match;
        if (msg.indexOf(re, 0, &match) == -1)
            return ;
        QString nickname = match.captured(1);

        // 优先遍历禁言的
        for (int i = blockedQueue.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = blockedQueue.at(i);
            if (!danmaku.is(MSG_DANMAKU))
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                if (eternalBlockUsers.contains(EternalBlockUser(danmaku.getUid(), roomId.toLongLong())))
                {
                    eternalBlockUsers.removeOne(EternalBlockUser(danmaku.getUid(), roomId.toLongLong()));
                    saveEternalBlockUsers();
                }

                delBlockUser(danmaku.getUid());
                sendNotifyMsg(">已解禁：" + nick, true);
                blockedQueue.removeAt(i);
                return ;
            }
        }

        // 其次遍历弹幕的
        for (int i = roomDanmakus.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = roomDanmakus.at(i);
            if (danmaku.getUid() == 0)
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                if (eternalBlockUsers.contains(EternalBlockUser(danmaku.getUid(), roomId.toLongLong())))
                {
                    eternalBlockUsers.removeOne(EternalBlockUser(danmaku.getUid(), roomId.toLongLong()));
                    saveEternalBlockUsers();
                }

                delBlockUser(danmaku.getUid());
                sendNotifyMsg(">已解禁：" + nick, true);
                blockedQueue.removeAt(i);
                return ;
            }
        }
    }
    else if (ui->enableBlockCheck->isChecked() && msg.startsWith("永久禁言 "))
    {
        QRegularExpression re("^永久禁言\\s*(\\S+)\\s*$");
        QRegularExpressionMatch match;
        if (msg.indexOf(re, 0, &match) == -1)
            return ;
        QString nickname = match.captured(1);
        for (int i = roomDanmakus.size()-1; i >= 0; i--)
        {
            const LiveDanmaku danmaku = roomDanmakus.at(i);
            if (!danmaku.is(MSG_DANMAKU))
                continue;

            QString nick = danmaku.getNickname();
            if (nick.contains(nickname))
            {
                eternalBlockUser(danmaku.getUid(), danmaku.getNickname());
                sendNotifyMsg(">已永久禁言：" + nick, true);
                return ;
            }
        }
    }
    else if (msg == "开启AI回复")
    {
        ui->AIReplyMsgCheck->setCheckState(Qt::CheckState::Checked);
        ui->AIReplyCheck->setChecked(true);
        on_AIReplyMsgCheck_clicked();
        if (!danmakuWindow)
            on_actionShow_Live_Danmaku_triggered();
        if (response)
            sendNotifyMsg(">已开启AI弹幕回复");
    }
    else if (msg == "关闭AI回复")
    {
        ui->AIReplyMsgCheck->setChecked(Qt::CheckState::Unchecked);
        on_AIReplyMsgCheck_clicked();
        if (settings->value("danmaku/aiReply", false).toBool())
            ui->AIReplyCheck->setChecked(false);
        if (response)
            sendNotifyMsg(">已关闭AI回复");
    }
    else
        return ;
    qDebug() << "执行远程命令：" << msg;
}

bool BaseWindow::execFunc(QString msg, CmdResponse &res, int &resVal)
{
    QRegularExpression re("^\\s*>");
    QRegularExpressionMatch match;
    int matchResult = -1;
    if (msg.indexOf(re) == -1)
        return false;

    qDebug() << "尝试执行命令：" << msg;
    auto RE = [=](QString exp) -> QRegularExpression {
        return QRegularExpression("^\\s*>\\s*" + exp + "\\s*$");
    };

    // 禁言
    if (msg.contains("block"))
    {
        re = RE("block\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            int hour = caps.at(2).toInt();
            addBlockUser(uid, hour);
            return true;
        }
        re = RE("block\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            int hour = ui->autoBlockTimeSpin->value();
            addBlockUser(uid, hour);
            return true;
        }
    }

    // 解禁言
    if (msg.contains("unblock"))
    {
        re = RE("unblock\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            delBlockUser(uid);
            return true;
        }
    }

    // 永久禁言
    if (msg.contains("eternalBlock"))
    {
        re = RE("eternalBlock\\s*\\(\\s*(\\d+)\\s*,\\s*(\\S+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            qint64 uid = caps.at(1).toLongLong();
            QString uname = caps.at(2);
            eternalBlockUser(uid, uname);
            return true;
        }
    }

    // 赠送礼物
    if (msg.contains("sendGift"))
    {
        re = RE("sendGift\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            int giftId = caps.at(1).toInt();
            int num = caps.at(2).toInt();
            sendGift(giftId, num);
            return true;
        }
    }

    // 终止
    if (msg.contains("abort"))
    {
        re = RE("abort\\s*(\\(\\s*\\))?");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            res = AbortRes;
            return true;
        }
    }

    // 延迟
    if (msg.contains("delay"))
    {
        re = RE("delay\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            int delay = caps.at(1).toInt(); // 单位：毫秒
            res = DelayRes;
            resVal = delay;
            return true;
        }
    }

    // 添加到游戏用户
    if (msg.contains("addGameUser"))
    {
        re = RE("addGameUser\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameUsers[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameUsers[chan].append(uid);
            return true;
        }

        re = RE("addGameUser\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameUsers[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameUsers[0].append(uid);
            return true;
        }
    }

    // 从游戏用户中移除
    if (msg.contains("removeGameUser"))
    {
        re = RE("removeGameUser\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameUsers[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameUsers[chan].removeOne(uid);
            return true;
        }

        re = RE("removeGameUser\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameUsers[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameUsers[0].removeOne(uid);
            return true;
        }
    }

    // 添加到游戏数值
    if (msg.contains("addGameNumber"))
    {
        re = RE("addGameNumber\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameNumberLists[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameNumberLists[chan].append(uid);
            saveGameNumbers(chan);
            return true;
        }

        re = RE("addGameNumber\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameNumberLists[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameNumberLists[0].append(uid);
            saveGameNumbers(0);
            return true;
        }
    }

    // 从游戏数值中移除
    if (msg.contains("removeGameNumber"))
    {
        re = RE("removeGameNumber\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameNumberLists[0].size();
            int chan = caps.at(1).toInt();
            qint64 uid = caps.at(2).toLongLong();
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameNumberLists[chan].removeOne(uid);
            saveGameNumbers(chan);
            return true;
        }

        re = RE("removeGameNumber\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameNumberLists[0].size();
            qint64 uid = caps.at(1).toLongLong();
            gameNumberLists[0].removeOne(uid);
            saveGameNumbers(0);
            return true;
        }
    }

    // 添加到文本数值
    if (msg.contains("addGameText"))
    {
        re = RE("addGameText\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameTextLists[0].size();
            int chan = caps.at(1).toInt();
            QString text = caps.at(2);
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameTextLists[chan].append(text);
            saveGameTexts(chan);
            return true;
        }

        re = RE("addGameText\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameTextLists[0].size();
            QString text = caps.at(1);
            gameTextLists[0].append(text);
            saveGameTexts(0);
            return true;
        }
    }

    // 从游戏文本中移除
    if (msg.contains("removeGameText"))
    {
        re = RE("removeGameText\\s*\\(\\s*(\\d{1,2})\\s*,\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameTextLists[0].size();
            int chan = caps.at(1).toInt();
            QString text = caps.at(2);
            if (chan < 0 || chan >= CHANNEL_COUNT)
                chan = 0;
            gameTextLists[chan].removeOne(text);
            saveGameTexts(chan);
            return true;
        }

        re = RE("removeGameText\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps << gameTextLists[0].size();
            QString text = caps.at(1);
            gameTextLists[0].removeOne(text);
            saveGameTexts(0);
            return true;
        }
    }

    // 执行远程命令
    if (msg.contains("execRemoteCommand"))
    {
        re = RE("execRemoteCommand\\s*\\(\\s*(.+?)\\s*,\\s*(\\d)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            int response = caps.at(2).toInt();
            qDebug() << "执行命令：" << caps;
            processRemoteCmd(cmd, response);
            return true;
        }

        re = RE("execRemoteCommand\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            qDebug() << "执行命令：" << caps;
            processRemoteCmd(cmd);
            return true;
        }
    }

    // 发送私信
    if (msg.contains("sendPrivateMsg"))
    {
        re = RE("sendPrivateMsg\\s*\\(\\s*(\\d+)\\s*,\\s*(\\S*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            QString msg = caps.at(2);
            qDebug() << "执行命令：" << caps;
            sendPrivateMsg(uid, msg);
            return true;
        }
    }

    // 发送指定直播间弹幕
    if (msg.contains("sendRoomMsg"))
    {
        re = RE("sendRoomMsg\\s*\\(\\s*(\\d+)\\s*,\\s*(\\S*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString roomId = caps.at(1);
            QString msg = caps.at(2);
            qDebug() << "执行命令：" << caps;
            sendRoomMsg(roomId, msg);
            return true;
        }
    }

    // 定时操作
    if (msg.contains("timerShot"))
    {
        re = RE("timerShot\\s*\\(\\s*(\\d+)\\s*,\\s*?(.*)\\s*?\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            int time = caps.at(1).toInt();
            QString msg = caps.at(2);
            QTimer::singleShot(time, this, [=]{
                QRegularExpression re("^\\s*>");
                if (msg.indexOf(re) > -1)
                {
                    CmdResponse res;
                    int resVal;
                    execFunc(msg, res, resVal);
                }
                else
                    sendAutoMsg(msg);
            });
            return true;
        }
    }

    // 发送本地通知
    if (msg.contains("localNotify"))
    {
        re = RE("localNotify\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString msg = caps.at(1);
            qDebug() << "执行命令：" << caps;
            localNotify(msg);
            return true;
        }

        re = RE("localNotify\\s*\\((\\d+)\\s*,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString uid = caps.at(1);
            QString msg = caps.at(2);
            qDebug() << "执行命令：" << caps;
            localNotify(msg, uid.toLongLong());
            return true;
        }
    }

    // 朗读文本
    if (msg.contains("speakText"))
    {
        re = RE("speakText\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qDebug() << "执行命令：" << caps;
            speakText(text);
            return true;
        }
    }

    // 网络操作
    if (msg.contains("openUrl"))
    {
        re = RE("openUrl\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString url = caps.at(1);
            qDebug() << "执行命令：" << caps;
            QDesktopServices::openUrl(url);
            return true;
        }
    }

    // 后台网络操作
    if (msg.contains("connectNet"))
    {
        re = RE("connectNet\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString url = caps.at(1);
            get(url, [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qDebug() << QString(ba);
            });
            return true;
        }
    }
    if (msg.contains("getData"))
    {
        re = RE("getData\\s*\\(\\s*(.+)\\s*,\\s*(\\S*?)\\s*\\)"); // 带参数二
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("getData\\s*\\(\\s*(.+)\\s*\\)"); // 不带参数二
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString callback = caps.size() > 2 ? caps.at(2) : "";
            get(url, [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qDebug() << QString(ba);
                if (!callback.isEmpty())
                {
                    triggerCmdEvent(callback, LiveDanmaku(MyJson(ba)));
                }
            });
            return true;
        }
    }
    if (msg.contains("postData"))
    {
        re = RE("postData\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*,\\s*(\\S+?)\\s*\\)"); // 带参数三
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("postData\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*\\)"); // 不带参数三
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString data = caps.at(2);
            QString callback = caps.size() > 3 ? caps.at(3) : "";
            post(url, data.toStdString().data(), [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qDebug() << QString(ba);
                if (!callback.isEmpty())
                {
                    triggerCmdEvent(callback, LiveDanmaku(MyJson(ba)));
                }
            });
            return true;
        }
    }
    if (msg.contains("postJson"))
    {
        re = RE("postJson\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*,\\s*(\\S+?)\\s*\\)"); // 带参数三
        if (msg.indexOf(re, 0, &match) == -1)
        {
            re = RE("postJson\\s*\\(\\s*(.+?)\\s*,\\s*(.*)\\s*\\)"); // 不带参数三
            if (msg.indexOf(re, 0, &match) == -1)
                return false;
        }
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString url = caps.at(1);
            QString data = caps.at(2);
            QString callback = caps.size() > 3 ? caps.at(3) : "";
            postJson(url, data.toStdString().data(), [=](QNetworkReply* reply){
                QByteArray ba(reply->readAll());
                qDebug() << QString(ba);
                if (!callback.isEmpty())
                {
                    triggerCmdEvent(callback, LiveDanmaku(MyJson(ba)));
                }
            });
            return true;
        }
    }

    // 发送socket
    if (msg.contains("sendToSockets"))
    {
        re = RE("sendToSockets\\s*\\(\\s*(\\S+),\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            QString data = caps.at(2);

            qDebug() << "执行命令：" << caps;
            sendTextToSockets(cmd, data.toUtf8());
            return true;
        }
    }
    if (msg.contains("sendToLastSocket"))
    {
        re = RE("sendToLastSocket\\s*\\(\\s*(\\S+),\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            QString data = caps.at(2);

            qDebug() << "执行命令：" << caps;
            if (danmakuSockets.size())
                sendTextToSockets(cmd, data.toUtf8(), danmakuSockets.last());
            return true;
        }
    }

    // 命令行
    if (msg.contains("runCommandLine"))
    {
        re = RE("runCommandLine\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString cmd = caps.at(1);
            qDebug() << "执行命令：" << caps;
            QProcess p(nullptr);
            p.start(cmd);
            p.waitForStarted();
            p.waitForFinished();
            qDebug() << QString::fromLocal8Bit(p.readAllStandardError());
            return true;
        }
    }

    // 打开文件
    if (msg.contains("openFile"))
    {
        re = RE("openFile\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString path = caps.at(1);
            qDebug() << "执行命令：" << caps;
#ifdef Q_OS_WIN32
            path = QString("file:///") + path;
            bool is_open = QDesktopServices::openUrl(QUrl(path, QUrl::TolerantMode));
            if(!is_open)
                qDebug() << "打开文件失败";
#else
            QString  cmd = QString("xdg-open ")+ path; //在linux下，可以通过system来xdg-open命令调用默认程序打开文件；
            system(cmd.toStdString().c_str());
#endif
            return true;
        }
    }

    // 写入文件行
    if (msg.contains("appendFileLine"))
    {
        re = RE("appendFileLine\\s*\\(\\s*(.*?)\\s*,\\s*(.+?)\\s*\\,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString dirName = caps.at(1);
            QString fileName = caps.at(2);
            QString format = caps.at(3);
            appendFileLine(dirName, fileName, format, lastDanmaku);
            return true;
        }
    }

    // 写入文件行
    if (msg.contains("writeTextFile"))
    {
        re = RE("writeTextFile\\s*\\(\\s*(.*?)\\s*,\\s*(.+?)\\s*\\,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString dirName = caps.at(1);
            QString fileName = caps.at(2);
            QString text = caps.at(3);
            writeTextFile(dirName + "/" + fileName, text);
            return true;
        }
    }

    // 删除文件
    if (msg.contains("removeFile"))
    {
        re = RE("appendFileLine\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString fileName = caps.at(1);
            if (fileName.startsWith("/"))
                fileName.replace(0, 1, "");
            QFile file(dataPath + fileName);
            file.remove();
            return true;
        }
    }

    // 播放音频文件
    if (msg.contains("playSound"))
    {
        re = RE("playSound\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString path = caps.at(1);
            qDebug() << "执行命令：" << caps;
            QMediaPlayer* player = new QMediaPlayer(this);
            player->setMedia(QUrl::fromLocalFile(path));
            connect(player, &QMediaPlayer::stateChanged, this, [=](QMediaPlayer::State state) {
                if (state == QMediaPlayer::StoppedState)
                    player->deleteLater();
            });
            player->play();
            return true;
        }
    }

    // 保存到配置
    if (msg.contains("setSetting"))
    {
        re = RE("setSetting\\s*\\(\\s*(\\S+?)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            QString value = caps.at(2);
            qDebug() << "执行命令：" << caps;
            settings->setValue(key, value);
            return true;
        }
    }

    // 删除配置
    if (msg.contains("removeSetting"))
    {
        re = RE("removeSetting\\s*\\(\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            qDebug() << "执行命令：" << caps;

            settings->remove(key);
            return true;
        }
    }

    // 保存到heaps
    if (msg.contains("setValue"))
    {
        re = RE("setValue\\s*\\(\\s*(\\S+?)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            QString value = caps.at(2);
            qDebug() << "执行命令：" << caps;
            heaps->setValue(key, value);
            return true;
        }
    }

    // 批量修改heaps
    if (msg.contains("setValues"))
    {
        re = RE("setValues\\s*\\(\\s*(\\S+?)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString value = caps.at(2);
            qDebug() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re) > -1)
                {
                    heaps->setValue(keys.at(i), value);
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 按条件批量修改heaps
    if (msg.contains("setValuesIf"))
    {
        re = RE("setValuesIf\\s*\\(\\s*(\\S+?)\\s*,\\s*\\[(.*?)\\]\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString VAL_EXP = caps.at(2);
            QString newValue = caps.at(3);
            qDebug() << "执行命令：" << caps;

            // 开始修改
            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            QRegularExpressionMatch match2;
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re, 0, &match2) > -1)
                {
                    QString exp = VAL_EXP;
                    // _VALUE_ 替换为 当前key的值
                    exp.replace("_VALUE_", heaps->value(keys.at(i)).toString());
                    // _$1_ 替换为 match的值
                    if (exp.contains("_$"))
                    {
                        auto caps = match2.capturedTexts();
                        for (int i = 0; i < caps.size(); i++)
                            exp.replace("_$" + snum(i) + "_", caps.at(i));
                    }
                    // 替换获取配置的值 _{}_
                    if (exp.contains("_{"))
                    {
                        QRegularExpression re2("_\\{(.*?)\\}_");
                        while (exp.indexOf(re2, 0, &match2) > -1)
                        {
                            QString _var = match2.captured(0);
                            QString key = match2.captured(1);
                            QVariant var = heaps->value(key);
                            exp.replace(_var, var.toString());
                        }
                    }
                    if (processVariantConditions(exp))
                    {
                        // 处理 newValue
                        if (newValue.contains("_VALUE_"))
                        {
                            // _VALUE_ 替换为 当前key的值
                            newValue.replace("_VALUE_", heaps->value(keys.at(i)).toString());

                            // 替换计算属性 _[]_
                            if (newValue.contains("_["))
                            {
                                QRegularExpression re2("_\\[(.*?)\\]_");
                                while (newValue.indexOf(re2, 0, &match2) > -1)
                                {
                                    QString _var = match2.captured(0);
                                    QString text = match2.captured(1);
                                    text = snum(calcIntExpression(text));
                                    newValue.replace(_var, text); // 默认使用变量类型吧
                                }
                            }
                        }

                        // 真正设置
                        heaps->setValue(keys.at(i), newValue);
                    }
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 删除heaps
    if (msg.contains("removeValue"))
    {
        re = RE("removeValue\\s*\\(\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            if (!key.contains("/"))
                key = "heaps/" + key;
            qDebug() << "执行命令：" << caps;

            heaps->remove(key);
            return true;
        }
    }

    // 批量删除heaps
    if (msg.contains("removeValues"))
    {
        re = RE("removeValues\\s*\\(\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            qDebug() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re) > -1)
                {
                    heaps->remove(keys.takeAt(i--));
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 按条件批量删除heaps
    if (msg.contains("removeValuesIf"))
    {
        re = RE("removeValuesIf\\s*\\(\\s*(\\S+?)\\s*,\\s*\\[(.*)\\]\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString key = caps.at(1);
            QString VAL_EXP = caps.at(2);
            qDebug() << "执行命令：" << caps;

            heaps->beginGroup("heaps");
            auto keys = heaps->allKeys();
            QRegularExpression re(key);
            QRegularExpressionMatch match2;
            for (int i = 0; i < keys.size(); i++)
            {
                if (keys.at(i).indexOf(re, 0, &match2) > -1)
                {
                    QString exp = VAL_EXP;
                    // _VALUE_ 替换为 当前key的值
                    exp.replace("_VALUE_", heaps->value(keys.at(i)).toString());
                    // _$1_ 替换为 match的值
                    if (exp.contains("_$"))
                    {
                        auto caps = match2.capturedTexts();
                        for (int i = 0; i < caps.size(); i++)
                            exp.replace("_$" + snum(i) + "_", caps.at(i));
                    }
                    // 替换获取配置的值 _{}_
                    if (exp.contains("_{"))
                    {
                        QRegularExpression re2("_\\{(.*?)\\}_");
                        while (exp.indexOf(re2, 0, &match2) > -1)
                        {
                            QString _var = match2.captured(0);
                            QString key = match2.captured(1);
                            QVariant var = heaps->value(key);
                            exp.replace(_var, var.toString());
                        }
                    }
                    if (processVariantConditions(exp))
                    {
                        heaps->remove(keys.takeAt(i--));
                    }
                }
            }
            heaps->endGroup();
            return true;
        }
    }

    // 提升点歌
    if (msg.contains("improveSongOrder"))
    {
        re = RE("improveSongOrder\\s*\\(\\s*(.+?)\\s*,\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString uname = caps.at(1);
            int promote = caps.at(2).toInt();
            qDebug() << "执行命令：" << caps;
            if (musicWindow)
            {
                musicWindow->improveUserSongByOrder(uname, promote);
            }
            else
            {
                localNotify("未开启点歌姬");
                qWarning() << "未开启点歌姬";
            }
            return true;
        }
    }

    // 切歌
    if (msg.contains("cutOrderSong"))
    {
        re = RE("cutOrderSong\\s*\\(\\s*(.+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString uname = caps.at(1);
            qDebug() << "执行命令：" << caps;
            if (musicWindow)
            {
                musicWindow->cutSongIfUser(uname);
            }
            else
            {
                localNotify("未开启点歌姬");
                qWarning() << "未开启点歌姬";
            }
            return true;
        }

        re = RE("cutOrderSong\\s*\\(\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            if (musicWindow)
            {
                musicWindow->cutSong();
            }
            else
            {
                localNotify("未开启点歌姬");
                qWarning() << "未开启点歌姬";
            }
            return true;
        }
    }

    // 提醒框
    if (msg.contains("messageBox"))
    {
        re = RE("messageBox\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qDebug() << "执行命令：" << caps;
            QMessageBox::information(this, "神奇弹幕", text);
            return true;
        }
    }

    // 发送长文本
    if (msg.contains("sendLongText"))
    {
        re = RE("sendLongText\\s*\\(\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qDebug() << "执行命令：" << caps;
            sendLongText(text);
            return true;
        }
    }

    // 执行自动回复任务
    if (msg.contains("triggerReply"))
    {
        re = RE("triggerReply\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qDebug() << "执行命令：" << caps;
            for (int i = 0; i < ui->replyListWidget->count(); i++)
            {
                auto rw = static_cast<ReplyWidget*>(ui->replyListWidget->itemWidget(ui->replyListWidget->item(i)));
                rw->triggerIfMatch(text, lastDanmaku);
            }
            return true;
        }
    }

    // 强制AI回复
    if (msg.contains("aiReply"))
    {
        re = RE("aiReply\\s*\\(\\s*(\\d+)\\s*,\\s*(.*?)\\s*(?:,\\s*(\\d+))?\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            qint64 id = caps.at(1).toLongLong();
            QString text = caps.at(2).trimmed();
            if (text.isEmpty())
                return true;
            int maxLen = ui->danmuLongestSpin->value(); // 默认只有一条弹幕的长度
            if (caps.size() > 3 && !caps.at(3).isEmpty())
                maxLen = caps.at(3).toInt();
            AIReply(id, text, [=](QString s){
                sendLongText(s);
            }, maxLen);
            return true;
        }
    }

    // 忽略自动欢迎
    if (msg.contains("ignoreWelcome"))
    {
        re = RE("ignoreWelcome\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            qDebug() << "执行命令：" << caps;

            if (!notWelcomeUsers.contains(uid))
            {
                notWelcomeUsers.append(uid);

                QStringList ress;
                foreach (qint64 uid, notWelcomeUsers)
                    ress << QString::number(uid);
                settings->setValue("danmaku/notWelcomeUsers", ress.join(";"));
            }

            return true;
        }
    }

    // 启用欢迎
    if (msg.contains("enableWelcome"))
    {
        re = RE("enableWelcome\\s*\\(\\s*(\\d+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            qDebug() << "执行命令：" << caps;

            if (notWelcomeUsers.contains(uid))
            {
                notWelcomeUsers.removeOne(uid);

                QStringList ress;
                foreach (qint64 uid, notWelcomeUsers)
                    ress << QString::number(uid);
                settings->setValue("danmaku/notWelcomeUsers", ress.join(";"));
            }

            return true;
        }
    }

    // 设置专属昵称
    if (msg.contains("setNickname"))
    {
        re = RE("setNickname\\s*\\(\\s*(\\d+)\\s*,\\s*(.*)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qint64 uid = caps.at(1).toLongLong();
            QString name = caps.at(2);
            qDebug() << "执行命令：" << caps;
            if (name.isEmpty()) // 移除
            {
                if (localNicknames.contains(uid))
                    localNicknames.remove(uid);
            }
            else // 添加
            {
                localNicknames[uid] = name;

                QStringList ress;
                auto it = localNicknames.begin();
                while (it != localNicknames.end())
                {
                    ress << QString("%1=>%2").arg(it.key()).arg(it.value());
                    it++;
                }
                settings->setValue("danmaku/localNicknames", ress.join(";"));
            }
            return true;
        }
    }

    // 开启大乱斗
    if (msg.contains("joinBattle"))
    {
        re = RE("joinBattle\\s*\\(\\s*([12])\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            int type = caps.at(1).toInt();
            qDebug() << "执行命令：" << caps;
            joinBattle(type);
            return true;
        }
    }

    // 自定义事件
    if (msg.contains("triggerEvent") || msg.contains("emitEvent"))
    {
        re = RE("(?:trigger|emit)Event\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qDebug() << "执行命令：" << caps;
            triggerCmdEvent(text, lastDanmaku);
            return true;
        }
    }

    // 点歌
    if (msg.contains("orderSong"))
    {
        re = RE("orderSong\\s*\\(\\s*(.+)\\s*,\\s*(.*?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            QString uname = caps.at(2);
            qDebug() << "执行命令：" << caps;
            if (!musicWindow)
                on_actionShow_Order_Player_Window_triggered();
            musicWindow->slotSearchAndAutoAppend(text, uname);
            return true;
        }
    }

    // 模拟快捷键
    if (msg.contains("simulateKeys"))
    {
        re = RE("simulateKeys\\s*\\(\\s*(.+)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            QString text = caps.at(1);
            qDebug() << "执行命令：" << caps;
            simulateKeys(text);
            return true;
        }
    }

    // 添加违禁词（到指定锚点）
    if (msg.contains("addBannedWord"))
    {
        re = RE("addBannedWord\\s*\\(\\s*(.+)\\s*,\\s*(\\S+?)\\s*\\)");
        if (msg.indexOf(re, 0, &match) > -1)
        {
            QStringList caps = match.capturedTexts();
            qDebug() << "执行命令：" << caps;
            QString word = caps.at(1);
            QString anchor = caps.at(2);
            addBannedWord(word, anchor);
            return true;
        }
    }

    return false;
}

void BaseWindow::simulateKeys(QString seq)
{
    if (seq.isEmpty())
        return ;

    // 模拟点击右键
    // mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    // mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    // keybd_event(VK_CONTROL, (BYTE) 0, 0, 0);
    // keybd_event('P', (BYTE)0, 0, 0);
    // keybd_event('P', (BYTE)0, KEYEVENTF_KEYUP, 0);
    // keybd_event(VK_CONTROL, (BYTE)0, KEYEVENTF_KEYUP, 0);
#if defined(Q_OS_WIN)
    // 字符串转KEY
    QList<int>keySeq;
    QStringList keyStrs = seq.toLower().split("+", QString::SkipEmptyParts);
    if (keyStrs.contains("ctrl"))
        keySeq.append(VK_CONTROL);
    if (keyStrs.contains("shift"))
        keySeq.append(VK_SHIFT);
    if (keyStrs.contains("alt"))
        keySeq.append(VK_MENU);
    keyStrs.removeOne("ctrl");
    keyStrs.removeOne("shift");
    keyStrs.removeOne("alt");
    for (int i = 0; i < keyStrs.size(); i++)
    {
        QString ch = keyStrs.at(i);
        if (ch.length() != 1)
            continue ;
        if (ch >= "0" && ch <= "9")
            keySeq.append(0x30 + ch.toInt());
        else if (ch >= "a" && ch <= "z")
            keySeq.append(0x41 + ch.at(0).toLatin1() - 'a');
    }

    // 开始模拟
    for (int i = 0; i < keySeq.size(); i++)
        keybd_event(keySeq.at(i), (BYTE) 0, 0, 0);

    for (int i = 0; i < keySeq.size(); i++)
        keybd_event(keySeq.at(i), (BYTE) 0, KEYEVENTF_KEYUP, 0);
#endif
}

QStringList BaseWindow::splitLongDanmu(QString text) const
{
    QStringList sl;
    int len = text.length();
    const int maxOne = danmuLongest;
    int count = (len + maxOne - 1) / maxOne;
    for (int i = 0; i < count; i++)
    {
        sl << text.mid(i * maxOne, maxOne);
    }
    return sl;
}

void BaseWindow::sendLongText(QString text)
{
    sendAutoMsg(splitLongDanmu(text).join("\\n"));
}

void BaseWindow::restoreCustomVariant(QString text)
{
    customVariant.clear();
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    foreach (QString s, sl)
    {
        QRegularExpression re("^\\s*(\\S+)\\s*=\\s?(.*)$");
        QRegularExpressionMatch match;
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            customVariant.append(QPair<QString, QString>(key, val));
        }
        else
            qCritical() << "自定义变量读取失败：" << s;
    }
}

QString BaseWindow::saveCustomVariant()
{
    QStringList sl;
    for (auto it = customVariant.begin(); it != customVariant.end(); ++it)
    {
        sl << it->first + " = " + it->second;
    }
    return sl.join("\n");
}

void BaseWindow::restoreVariantTranslation()
{
    variantTranslation.clear();

    // 变量
    QString text = readTextFile(":/documents/translation_variables");
    QStringList sl = text.split("\n", QString::SkipEmptyParts);
    QRegularExpression re("^\\s*(\\S+)\\s*=\\s?(.*)$");
    QRegularExpressionMatch match;
    foreach (QString s, sl)
    {
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            key = "%" + key + "%";
            val = "%" + val + "%";
            variantTranslation.append(QPair<QString, QString>(key, val));
        }
        else
            qCritical() << "多语言翻译读取失败：" << s;
    }

    // 方法
    text = readTextFile(":/documents/translation_methods");
    sl = text.split("\n", QString::SkipEmptyParts);
    re = QRegularExpression("^\\s*(\\S+)\\s*=\\s?(.*)$");
    foreach (QString s, sl)
    {
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            key = ">" + key + "(";
            val = ">" + val + "(";
            variantTranslation.append(QPair<QString, QString>(key, val));
        }
        else
            qCritical() << "多语言翻译读取失败：" << s;
    }

    // 函数
    text = readTextFile(":/documents/translation_functions");
    sl = text.split("\n", QString::SkipEmptyParts);
    re = QRegularExpression("^\\s*(\\S+)\\s*=\\s?(.*)$");
    foreach (QString s, sl)
    {
        if (s.indexOf(re, 0, &match) != -1)
        {
            QString key = match.captured(1);
            QString val = match.captured(2);
            key = ">" + key + "(";
            val = ">" + val + "(";
            variantTranslation.append(QPair<QString, QString>(key, val));
        }
        else
            qCritical() << "多语言翻译读取失败：" << s;
    }
}

void BaseWindow::saveOrderSongs(const SongList &songs)
{
    int count = qMin(songs.size(), ui->orderSongsToFileMaxSpin->value());
    QString format = ui->orderSongsToFileFormatEdit->text();

    Song currentSong = musicWindow ? musicWindow->getPlayingSong() : Song();

    // 组合成长文本
    QStringList sl;
    for (int i = 0; i < count; i++)
    {
        Song song = songs.at(i);
        QString text = format;
        text = text.replace("{序号}", snum(i+1))
                .replace("{歌名}", song.name)
                .replace("{歌手}", song.artistNames)
                .replace("{用户}", song.addBy)
                .replace("{时长}", snum(song.duration/60) + ":" + snum(song.duration%60))
                .replace("{专辑}", song.album.name)
                .replace("{当前歌名}", currentSong.name)
                .replace("{当前歌手}", currentSong.artistNames)
                .replace("{当前用户}", currentSong.addBy);
        sl.append(text);
    }
    if (!sl.size()) // 直播姬会跳过空文本文件，所以需要设置个空格
        sl.append(" ");

    // 获取路径
    QDir dir(wwwDir.absoluteFilePath("music"));
    dir.mkpath(dir.absolutePath());

    // 保存到文件
    QFile file(dir.absoluteFilePath("songs.txt"));
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    if (!externFileCodec.isEmpty())
        stream.setCodec(externFileCodec.toUtf8());
    stream << sl.join("\n");
    file.flush();
    file.close();
}

void BaseWindow::saveSongLyrics()
{
    QStringList lyrics = musicWindow->getSongLyrics(ui->songLyricsToFileMaxSpin->value());
    if (!lyrics.size()) // 直播姬会跳过空文本文件，所以需要设置个空格
        lyrics.append(" ");

    // 获取路径
    QDir dir(wwwDir.absoluteFilePath("music"));
    dir.mkpath(dir.absolutePath());

    // 保存到文件
    QFile file(dir.absoluteFilePath("lyrics.txt"));
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    if (!externFileCodec.isEmpty())
        stream.setCodec(externFileCodec.toUtf8());
    stream << lyrics.join("\n");
    file.flush();
    file.close();
}

void BaseWindow::slotBinaryMessageReceived(const QByteArray &message)
{
    int operation = ((uchar)message[8] << 24)
            + ((uchar)message[9] << 16)
            + ((uchar)message[10] << 8)
            + ((uchar)message[11]);
    QByteArray body = message.right(message.length() - 16);
    SOCKET_DEB << "操作码=" << operation << "  大小=" << body.size() << "  正文=" << (body.left(1000)) << "...";

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(body, &error);
    QJsonObject json;
    if (error.error == QJsonParseError::NoError)
        json = document.object();

    if (operation == AUTH_REPLY) // 认证包回复
    {
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("认证出错");
        }
    }
    else if (operation == HEARTBEAT_REPLY) // 心跳包回复（人气值）
    {
        qint32 popularity = ((uchar)body[0] << 24)
                + ((uchar)body[1] << 16)
                + ((uchar)body[2] << 8)
                + (uchar)body[3];
        SOCKET_DEB << "人气值=" << popularity;
        this->popularVal = this->currentPopul = popularity;
        if (isLiving())
            ui->popularityLabel->setText("人气值：" + QString::number(popularity));
    }
    else if (operation == SEND_MSG_REPLY) // 普通包
    {
        QString cmd;
        if (!json.isEmpty())
        {
            cmd = json.value("cmd").toString();

            if (cmd == "STOP_LIVE_ROOM_LIST" || cmd == "NOTICE_MSG")
                return ;

            qDebug() << "普通CMD：" << cmd;
            SOCKET_INF << json;
        }

        if (cmd == "NOTICE_MSG") // 全站广播（不用管）
        {

        }
        else if (cmd == "ROOM_RANK")
        {
            /*{
                "cmd": "ROOM_RANK",
                "data": {
                    "color": "#FB7299",
                    "h5_url": "https://live.bilibili.com/p/html/live-app-rankcurrent/index.html?is_live_half_webview=1&hybrid_half_ui=1,5,85p,70p,FFE293,0,30,100,10;2,2,320,100p,FFE293,0,30,100,0;4,2,320,100p,FFE293,0,30,100,0;6,5,65p,60p,FFE293,0,30,100,10;5,5,55p,60p,FFE293,0,30,100,10;3,5,85p,70p,FFE293,0,30,100,10;7,5,65p,60p,FFE293,0,30,100,10;&anchor_uid=688893202&rank_type=master_realtime_area_hour&area_hour=1&area_v2_id=145&area_v2_parent_id=1",
                    "rank_desc": "娱乐小时榜 7",
                    "roomid": 22532956,
                    "timestamp": 1605749940,
                    "web_url": "https://live.bilibili.com/blackboard/room-current-rank.html?rank_type=master_realtime_area_hour&area_hour=1&area_v2_id=145&area_v2_parent_id=1"
                }
            }*/
            QJsonObject data = json.value("data").toObject();
            QString color = data.value("color").toString();
            QString desc = data.value("rank_desc").toString();
            ui->roomRankLabel->setStyleSheet("color: " + color + ";");
            ui->roomRankLabel->setText(desc);
            ui->roomRankLabel->setToolTip(QDateTime::currentDateTime().toString("更新时间：hh:mm:ss"));
            if (desc != ui->roomRankLabel->text()) // 排名有更新
                localNotify("当前排名：" + desc);

            triggerCmdEvent(cmd, LiveDanmaku());
        }
        else if (handlePK(json))
        {

        }
        else // 压缩弹幕消息
        {
            short protover = (message[6]<<8) + message[7];
            SOCKET_INF << "协议版本：" << protover;
            if (protover == 2) // 默认协议版本，zlib解压
            {
                slotUncompressBytes(body);
            }
            else if (protover == 0)
            {
                QJsonDocument document = QJsonDocument::fromJson(body, &error);
                if (error.error != QJsonParseError::NoError)
                {
                    qCritical() << s8("body转json出错：") << error.errorString();
                    return ;
                }
                QJsonObject json = document.object();
                QString cmd = json.value("cmd").toString();

                if (cmd == "STOP_LIVE_ROOM_LIST" || cmd == "WIDGET_BANNER")
                    return ;

                qDebug() << ">消息命令UNZC：" << cmd;

                if (cmd == "ROOM_RANK")
                {
                }
                else if (cmd == "ROOM_REAL_TIME_MESSAGE_UPDATE") // 实时信息改变
                {
                    // {"cmd":"ROOM_REAL_TIME_MESSAGE_UPDATE","data":{"roomid":22532956,"fans":1022,"red_notice":-1,"fans_club":50}}
                    QJsonObject data = json.value("data").toObject();
                    int fans = data.value("fans").toInt();
                    int fans_club = data.value("fans_club").toInt();
                    int delta_fans = 0, delta_club = 0;
                    if (currentFans || currentFansClub)
                    {
                        delta_fans = fans - currentFans;
                        delta_club = fans_club - currentFansClub;
                    }
                    currentFans = fans;
                    currentFansClub = fans_club;
                    qDebug() << s8("粉丝数量：") << fans << s8("  粉丝团：") << fans_club;
                    // appendNewLiveDanmaku(LiveDanmaku(fans, fans_club, delta_fans, delta_club));

                    dailyNewFans += delta_fans;
                    if (dailySettings)
                    {
                        dailySettings->setValue("new_fans", dailyNewFans);
                        dailySettings->setValue("total_fans", currentFans);
                    }

//                    if (delta_fans) // 如果有变动，实时更新
//                        getFansAndUpdate();
                    fansLabel->setText("粉丝:" + snum(fans));
                    fansLabel->setToolTip("粉丝数量：" + snum(fans) + "，粉丝团：" + snum(fans_club) + (currentGuards.size() ? "，船员数：" + snum(currentGuards.size()) : ""));
                }
                else if (cmd == "WIDGET_BANNER") // 无关的横幅广播
                {}
                else if (cmd == "HOT_RANK_CHANGED") // 热门榜
                {
                    /*{
                        "cmd": "HOT_RANK_CHANGED",
                        "data": {
                            "rank": 14,
                            "trend": 2, // 趋势：1上升，2下降
                            "countdown": 1705,
                            "timestamp": 1610168495,
                            "web_url": "https://live.bilibili.com/p/html/live-app-hotrank/index.html?clientType=2\\u0026area_id=1",
                            "live_url": "……（太长了）",
                            "blink_url": "……（太长了）",
                            "live_link_url": "……（太长了）",
                            "pc_link_url": "……（太长了）",
                            "icon": "https://i0.hdslb.com/bfs/live/3f833451003cca16a284119b8174227808d8f936.png",
                            "area_name": "娱乐"
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int rank = data.value("rank").toInt();
                    int trend = data.value("trend").toInt(); // 趋势：1上升，2下降
                    QString area_name = data.value("area_name").toString();
                    if (area_name.endsWith("榜"))
                        area_name.replace(area_name.length()-1, 1, "");
                    QString msg = QString("热门榜 " + area_name + "榜 排名：" + snum(rank) + " " + (trend == 1 ? "↑" : "↓"));
                    rankLabel->setText(area_name + "榜 " + snum(rank) + " " + (trend == 1 ? "↑" : "↓"));
                    rankLabel->setToolTip(msg);
                }
                else if (cmd == "HOT_RANK_SETTLEMENT")
                {
                    /*{
                        "cmd": "HOT_RANK_SETTLEMENT",
                        "data": {
                            "rank": 9,
                            "uname": "丸嘻嘻",
                            "face": "http://i2.hdslb.com/bfs/face/17f1f3994cb4b2bba97f1557ffc7eb34a05e119b.jpg",
                            "timestamp": 1610173800,
                            "icon": "https://i0.hdslb.com/bfs/live/3f833451003cca16a284119b8174227808d8f936.png",
                            "area_name": "娱乐",
                            "url": "https://live.bilibili.com/p/html/live-app-hotrank/result.html?is_live_half_webview=1\\u0026hybrid_half_ui=1,5,250,200,f4eefa,0,30,0,0,0;2,5,250,200,f4eefa,0,30,0,0,0;3,5,250,200,f4eefa,0,30,0,0,0;4,5,250,200,f4eefa,0,30,0,0,0;5,5,250,200,f4eefa,0,30,0,0,0;6,5,250,200,f4eefa,0,30,0,0,0;7,5,250,200,f4eefa,0,30,0,0,0;8,5,250,200,f4eefa,0,30,0,0,0\\u0026areaId=1\\u0026cache_key=4417cab3fa8b15ad1b250ee29fd91c52",
                            "cache_key": "4417cab3fa8b15ad1b250ee29fd91c52",
                            "dm_msg": "恭喜主播 \\u003c% 丸嘻嘻 %\\u003e 荣登限时热门榜娱乐榜top9! 即将获得热门流量推荐哦！"
                        }
                    }*/
                    QJsonObject data = json.value("data").toObject();
                    int rank = data.value("rank").toInt();
                    QString uname = data.value("uname").toString();
                    QString area_name = data.value("area_name").toString();
                    QString msg = QString("恭喜荣登热门榜" + area_name + "榜 top" + snum(rank) + "!");
                    triggerCmdEvent("HOT_RANK", LiveDanmaku(area_name + "榜 top" + snum(rank)));
                    localNotify(msg);
                }
                else if (cmd == "PK_BATTLE_START_NEW")
                {
                    /*{
                        "cmd": "PK_BATTLE_START_NEW",
                        "pk_id": 200271102,
                        "pk_status": 201,
                        "timestamp": 1611152129,
                        "data": {
                            "battle_type": 1,
                            "final_hit_votes": 0,
                            "pk_start_time": 1611152129,
                            "pk_frozen_time": 1611152429,
                            "pk_end_time": 1611152439,
                            "pk_votes_type": 0,
                            "pk_votes_add": 0,
                            "pk_votes_name": "\\u4e71\\u6597\\u503c"
                        }
                    }*/
                }
                else if (cmd == "PK_BATTLE_PROCESS_NEW")
                {
                    /*{
                        "cmd": "PK_BATTLE_PROCESS_NEW",
                        "pk_id": 200270835,
                        "pk_status": 201,
                        "timestamp": 1611151874,
                        "data": {
                            "battle_type": 1,
                            "init_info": {
                                "room_id": 2603963,
                                "votes": 55,
                                "best_uname": "\\u963f\\u5179\\u963f\\u5179\\u7684\\u77db"
                            },
                            "match_info": {
                                "room_id": 22532956,
                                "votes": 184,
                                "best_uname": "\\u591c\\u7a7a\\u3001"
                            }
                        }
                    }*/
                }
                else if (cmd == "PK_BATTLE_RANK_CHANGE")
                {
                    /*{
                        "cmd": "PK_BATTLE_RANK_CHANGE",
                        "timestamp": 1611152461,
                        "data": {
                            "first_rank_img_url": "https:\\/\\/i0.hdslb.com\\/bfs\\/live\\/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                            "rank_name": "\\u767d\\u94f6\\u6597\\u58ebx1\\u661f"
                        }
                    }*/
                }
                else if (cmd == "PK_BATTLE_SETTLE_NEW")
                {
                    /*{
                        "cmd": "PK_BATTLE_SETTLE_NEW",
                        "pk_id": 200933662,
                        "pk_status": 601,
                        "timestamp": 1613959764,
                        "data": {
                            "pk_id": 200933662,
                            "pk_status": 601,
                            "settle_status": 1,
                            "punish_end_time": 1613959944,
                            "timestamp": 1613959764,
                            "battle_type": 6,
                            "init_info": {
                                "room_id": 7259049,
                                "result_type": -1,
                                "votes": 0,
                                "assist_info": []
                            },
                            "match_info": {
                                "room_id": 21839758,
                                "result_type": 2,
                                "votes": 3,
                                "assist_info": [
                                    {
                                        "rank": 1,
                                        "uid": 412357310,
                                        "face": "http:\\/\\/i0.hdslb.com\\/bfs\\/face\\/e97fbf0e412b936763033055821e1ff5df56565a.jpg",
                                        "uname": "\\u6cab\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8\\u58a8"
                                    }
                                ]
                            },
                            "dm_conf": {
                                "font_color": "#FFE10B",
                                "bg_color": "#72C5E2"
                            }
                        }
                    }*/
                }
                else
                {
                    qWarning() << "未处理的命令=" << cmd << "   正文=" << QString(body);
                }

                triggerCmdEvent(cmd, LiveDanmaku(json.value("data").toObject()));
            }
            else
            {
                qWarning() << s8("未知协议：") << protover << s8("，若有必要请处理");
                qDebug() << s8("未知正文：") << body;
            }
        }
    }
    else
    {

    }
//    delete[] body.data();
//    delete[] message.data();
    SOCKET_DEB << "消息处理结束";
}

/**
 * 博客来源：https://blog.csdn.net/doujianyoutiao/article/details/106236207
 */
QByteArray zlibToQtUncompr(const char *pZLIBData, uLongf dataLen/*, uLongf srcDataLen = 0x100000*/)
{
    char *pQtData = new char[dataLen + 4];
    char *pByte = (char *)(&dataLen);/*(char *)(&srcDataLen);*/
    pQtData[3] = *pByte;
    pQtData[2] = *(pByte + 1);
    pQtData[1] = *(pByte + 2);
    pQtData[0] = *(pByte + 3);
    memcpy(pQtData + 4, pZLIBData, dataLen);
    QByteArray qByteArray(pQtData, dataLen + 4);
    delete[] pQtData;
    return qUncompress(qByteArray);
}

void BaseWindow::slotUncompressBytes(const QByteArray &body)
{
    splitUncompressedBody(zlibToQtUncompr(body.data(), body.size()+1));
}

void BaseWindow::splitUncompressedBody(const QByteArray &unc)
{
    int offset = 0;
    short headerSize = 16;
    while (offset < unc.size() - headerSize)
    {
        int packSize = ((uchar)unc[offset+0] << 24)
                + ((uchar)unc[offset+1] << 16)
                + ((uchar)unc[offset+2] << 8)
                + (uchar)unc[offset+3];
        QByteArray jsonBa = unc.mid(offset + headerSize, packSize - headerSize);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << s8("解析解压后的JSON出错：") << error.errorString();
            qCritical() << s8("包数值：") << offset << packSize << "  解压后大小：" << unc.size();
            qCritical() << s8(">>当前JSON") << jsonBa;
            qCritical() << s8(">>解压正文") << unc;
            return ;
        }
        QJsonObject json = document.object();
        QString cmd = json.value("cmd").toString();
        SOCKET_INF << "解压后获取到CMD：" << cmd;
        if (cmd != "ROOM_BANNER" && cmd != "ACTIVITY_BANNER_UPDATE_V2" && cmd != "PANEL"
                && cmd != "ONLINERANK")
            SOCKET_INF << "单个JSON消息：" << offset << packSize << QString(jsonBa);
        try {
            handleMessage(json);
        } catch (...) {
            qCritical() << s8("出错啦") << jsonBa;
        }

        offset += packSize;
    }
}

/**
 * 数据包解析： https://segmentfault.com/a/1190000017328813?utm_source=tag-newest#tagDataPackage
 */
void BaseWindow::handleMessage(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    qDebug() << s8(">消息命令ZCOM：") << cmd;
    if (cmd == "LIVE") // 开播？
    {
        if (ui->recordCheck->isChecked())
            startLiveRecord();
        emit signalLiveStart(roomId);

        if (isLiving() || pking || pkToLive + 30 > QDateTime::currentSecsSinceEpoch()) // PK导致的开播下播情况
        {
            qDebug() << "忽视PK导致的开播情况";
            // 大乱斗时突然断联后恢复
            if (!isLiving())
            {
                if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
                    connectServerTimer->stop();
                slotStartWork();
            }
            return ;
        }
        QString roomId = json.value("roomid").toString();
//        if (roomId == this->roomId || roomId == this->shortId) // 是当前房间的
        {
            QString text = ui->startLiveWordsEdit->text();
            if (ui->startLiveSendCheck->isChecked() && !text.trimmed().isEmpty()
                    && QDateTime::currentMSecsSinceEpoch() - liveTimestamp > 600000) // 起码是开播十分钟后
                sendAutoMsg(text);
            ui->popularityLabel->setText("已开播");
            liveStatus = 1;
            if (ui->timerConnectServerCheck->isChecked() && connectServerTimer->isActive())
                connectServerTimer->stop();
            slotStartWork(); // 每个房间第一次开始工作
        }
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "PREPARING") // 下播
    {
        finishLiveRecord();

        if (pking || pkToLive + 30 > QDateTime::currentSecsSinceEpoch()) // PK导致的开播下播情况
            return ;
        QString roomId = json.value("roomid").toString();
//        if (roomId == this->roomId || roomId == this->shortId) // 是当前房间的
        {
            QString text = ui->endLiveWordsEdit->text();
            if (ui->startLiveSendCheck->isChecked() &&!text.trimmed().isEmpty()
                    && QDateTime::currentMSecsSinceEpoch() - liveTimestamp > 600000) // 起码是十分钟后再播报，万一只是尝试开播呢
                sendAutoMsg(text);
            ui->popularityLabel->setText("已下播");
            liveStatus = 0;

            if (ui->timerConnectServerCheck->isChecked() && !connectServerTimer->isActive())
                connectServerTimer->start();
        }

        releaseLiveData(true);
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ROOM_CHANGE")
    {
        getRoomInfo(false);
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "DANMU_MSG") // 收到弹幕
    {
        QJsonArray info = json.value("info").toArray();
        if (info.size() <= 2)
            QMessageBox::information(this, "弹幕数据 info", QString(QJsonDocument(info).toJson()));
        QJsonArray array = info[0].toArray();
        if (array.size() <= 3)
            QMessageBox::information(this, "弹幕数据 array", QString(QJsonDocument(array).toJson()));
        qint64 textColor = array[3].toInt(); // 弹幕颜色
        qint64 timestamp = static_cast<qint64>(array[4].toDouble());
        QString msg = info[1].toString();
        QJsonArray user = info[2].toArray();
        if (user.size() <= 1)
            QMessageBox::information(this, "弹幕数据 user", QString(QJsonDocument(user).toJson()));
        qint64 uid = static_cast<qint64>(user[0].toDouble());
        QString username = user[1].toString();
        int admin = user[2].toInt(); // 是否为房管（实测现在主播不属于房管了）
        int vip = user[3].toInt(); // 是否为老爷
        int svip = user[4].toInt(); // 是否为年费老爷
        int uidentity = user[5].toInt(); // 是否为非正式会员或正式会员（5000非，10000正）
        int iphone = user[6].toInt(); // 是否绑定手机
        QString unameColor = user[7].toString();
        int level = info[4].toArray()[0].toInt();
        QJsonArray medal = info[3].toArray();
        int uguard = info[7].toInt(); // 用户本房间舰队身份：0非，1总督，2提督，3舰长
        int medal_level = 0;

        bool opposite = pking &&
                ((oppositeAudience.contains(uid) && !myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && medal.size() >= 4 &&
                     snum(static_cast<qint64>(medal[3].toDouble())) == pkRoomId));

        // !弹幕的时间戳是13位，其他的是10位！
        qDebug() << s8("接收到弹幕：") << username << msg << QDateTime::fromMSecsSinceEpoch(timestamp);
        /*QString localName = danmakuWindow->getLocalNickname(uid);
        if (!localName.isEmpty())
            username = localName;*/

        // 统计弹幕次数
        int danmuCount = danmakuCounts->value("danmaku/"+snum(uid), 0).toInt()+1;
        danmakuCounts->setValue("danmaku/"+snum(uid), danmuCount);
        dailyDanmaku++;
        if (dailySettings)
            dailySettings->setValue("danmaku", dailyDanmaku);

        // 添加到列表
        QString cs = QString::number(textColor, 16);
        while (cs.size() < 6)
            cs = "0" + cs;
        LiveDanmaku danmaku(username, msg, uid, level, QDateTime::fromMSecsSinceEpoch(timestamp),
                                                 unameColor, "#"+cs);
        danmaku.setUserInfo(admin, vip, svip, uidentity, iphone, uguard);
        if (medal.size() >= 4)
        {
            medal_level = medal[0].toInt();
            danmaku.setMedal(snum(static_cast<qint64>(medal[3].toDouble())),
                    medal[1].toString(), medal_level, medal[2].toString());
        }
        if (snum(uid) == cookieUid && noReplyMsgs.contains(msg))
        {
            danmaku.setNoReply();
            noReplyMsgs.removeOne(msg);
        }
        else
            minuteDanmuPopul++;
        danmaku.setOpposite(opposite);
        appendNewLiveDanmaku(danmaku);

        // 新人发言
        if (danmuCount == 1)
        {
            dailyNewbieMsg++;
            if (dailySettings)
                dailySettings->setValue("newbie_msg", dailyNewbieMsg);
        }

        // 新人小号禁言
        bool blocked = false;
        auto testTipBlock = [&]{
            if (danmakuWindow && !ui->promptBlockNewbieKeysEdit->toPlainText().trimmed().isEmpty())
            {
                QString reStr = ui->promptBlockNewbieKeysEdit->toPlainText();
                if (reStr.endsWith("|"))
                    reStr = reStr.left(reStr.length()-1);
                if (msg.indexOf(QRegularExpression(reStr)) > -1) // 提示拉黑
                {
                    blocked = true;
                    danmakuWindow->showFastBlock(uid, msg);
                }
            }
        };
        if (snum(uid) == upUid || snum(uid) == cookieUid) // 是自己或UP主的，不屏蔽
        {
            // 不仅不屏蔽，反而支持主播特权
            processRemoteCmd(msg);
        }
        else if (admin && ui->allowAdminControlCheck->isChecked())
        {
            // 开放给房管的特权
            processRemoteCmd(msg);
        }
        else if (ui->blockNotOnlyNewbieCheck->isChecked() || (level == 0 && medal_level <= 1 && danmuCount <= 3) || danmuCount <= 1)
        {
            // 尝试自动拉黑
            if (ui->autoBlockNewbieCheck->isChecked() && !ui->autoBlockNewbieKeysEdit->toPlainText().trimmed().isEmpty())
            {
                QString reStr = ui->autoBlockNewbieKeysEdit->toPlainText();
                if (reStr.endsWith("|"))
                    reStr = reStr.left(reStr.length()-1);
                QRegularExpression re(reStr);
                QRegularExpressionMatch match;
                if (msg.indexOf(re, 0, &match) > -1 // 自动拉黑
                        && danmaku.getAnchorRoomid() != roomId // 不带有本房间粉丝牌
                        && !isInFans(uid) // 未刚关注主播（新人一般都是刚关注吧，在第一页）
                        && medal_level <= 2 // 勋章不到3级
                        )
                {
                    if (match.capturedTexts().size() > 1)
                    {
                        QString blockKey = match.captured(1); // 第一个括号的
                        localNotify("检测到新人说【" + blockKey + "】，自动禁言");
                    }
                    qDebug() << "检测到新人违禁词，自动拉黑：" << username << msg;

                    // 拉黑
                    addBlockUser(uid, ui->autoBlockTimeSpin->value());
                    blocked = true;

                    // 通知
                    if (ui->autoBlockNewbieNotifyCheck->isChecked())
                    {
                        static int prevNotifyInCount = -20; // 上次发送通知时的弹幕数量
                        if (allDanmakus.size() - prevNotifyInCount >= 20) // 最低每20条发一遍
                        {
                            prevNotifyInCount = allDanmakus.size();

                            QStringList words = getEditConditionStringList(ui->autoBlockNewbieNotifyWordsEdit->toPlainText(), danmaku);
                            if (words.size())
                            {
                                int r = qrand() % words.size();
                                QString s = words.at(r);
                                if (!s.trimmed().isEmpty())
                                {
                                    sendNotifyMsg(s);
                                }
                            }
                            else if (debugPrint)
                            {
                                localNotify("[没有可发送的禁言通知弹幕]");
                            }
                        }
                    }
                }
            }

            // 提示拉黑
            if (!blocked && ui->promptBlockNewbieCheck->isChecked())
            {
                testTipBlock();
            }
        }
        else if (ui->promptBlockNewbieCheck->isChecked() && ui->notOnlyNewbieCheck->isChecked())
        {
            // 提示拉黑
            testTipBlock();
        }

        if (!blocked)
            markNotRobot(uid);

        triggerCmdEvent(cmd, danmaku);
    }
    else if (cmd == "SEND_GIFT") // 有人送礼
    {
        /*{
            "cmd": "SEND_GIFT",
            "data": {
                "draw": 0,
                "gold": 0,
                "silver": 0,
                "num": 1,
                "total_coin": 0,
                "effect": 0,
                "broadcast_id": 0,
                "crit_prob": 0,
                "guard_level": 0,
                "rcost": 200773,
                "uid": 20285041,
                "timestamp": 1614439816,
                "giftId": 30607,
                "giftType": 5,
                "super": 0,
                "super_gift_num": 1,
                "super_batch_gift_num": 1,
                "remain": 19,
                "price": 0,
                "beatId": "",
                "biz_source": "Live",
                "action": "投喂",
                "coin_type": "silver",
                "uname": "懒一夕智能科技",
                "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
                "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30607:1614439816.1655",      // 多个以及最后的 COMBO_SEND 是一样的
                "rnd": "34158224",
                "giftName": "小心心",
                "combo_send": null,
                "batch_combo_send": null,
                "tag_image": "",
                "top_list": null,
                "send_master": null,
                "is_first": true,                   // 不是第一个就是false
                "demarcation": 1,
                "combo_stay_time": 3,
                "combo_total_coin": 1,
                "tid": "1614439816120100003",
                "effect_block": 1,
                "is_special_batch": 0,
                "combo_resources_id": 1,
                "magnification": 1,
                "name_color": "",
                "medal_info": {
                    "target_id": 0,
                    "special": "",
                    "icon_id": 0,
                    "anchor_uname": "",
                    "anchor_roomid": 0,
                    "medal_level": 0,
                    "medal_name": "",
                    "medal_color": 0,
                    "medal_color_start": 0,
                    "medal_color_end": 0,
                    "medal_color_border": 0,
                    "is_lighted": 0,
                    "guard_level": 0
                },
                "svga_block": 0
            }
        }*/
        /*{
            "cmd": "SEND_GIFT",
            "data": {
                "draw": 0,
                "gold": 0,
                "silver": 0,
                "num": 1,
                "total_coin": 1000,
                "effect": 0,                // 太便宜的礼物没有
                "broadcast_id": 0,
                "crit_prob": 0,
                "guard_level": 0,
                "rcost": 200795,
                "uid": 20285041,
                "timestamp": 1614440753,
                "giftId": 30823,
                "giftType": 0,
                "super": 0,
                "super_gift_num": 1,
                "super_batch_gift_num": 1,
                "remain": 0,
                "price": 1000,
                "beatId": "",
                "biz_source": "Live",
                "action": "投喂",
                "coin_type": "gold",
                "uname": "懒一夕智能科技",
                "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
                "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30823:1614440753.1786",
                "rnd": "245758485",
                "giftName": "小巧花灯",
                "combo_send": {
                    "uid": 20285041,
                    "gift_num": 1,
                    "combo_num": 1,
                    "gift_id": 30823,
                    "combo_id": "gift:combo_id:20285041:2070473390:30823:1614440753.1781",
                    "gift_name": "小巧花灯",
                    "action": "投喂",
                    "uname": "懒一夕智能科技",
                    "send_master": null
                },
                "batch_combo_send": {
                    "uid": 20285041,
                    "gift_num": 1,
                    "batch_combo_num": 1,
                    "gift_id": 30823,
                    "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30823:1614440753.1786",
                    "gift_name": "小巧花灯",
                    "action": "投喂",
                    "uname": "懒一夕智能科技",
                    "send_master": null
                },
                "tag_image": "",
                "top_list": null,
                "send_master": null,
                "is_first": true,
                "demarcation": 2,
                "combo_stay_time": 3,
                "combo_total_coin": 1000,
                "tid": "1614440753121100001",
                "effect_block": 0,
                "is_special_batch": 0,
                "combo_resources_id": 1,
                "magnification": 1,
                "name_color": "",
                "medal_info": {
                    "target_id": 0,
                    "special": "",
                    "icon_id": 0,
                    "anchor_uname": "",
                    "anchor_roomid": 0,
                    "medal_level": 0,
                    "medal_name": "",
                    "medal_color": 0,
                    "medal_color_start": 0,
                    "medal_color_end": 0,
                    "medal_color_border": 0,
                    "is_lighted": 0,
                    "guard_level": 0
                },
                "svga_block": 0
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        int giftId = data.value("giftId").toInt();
        int giftType = data.value("giftType").toInt(); // 不知道是啥，金瓜子1，银瓜子（小心心、辣条）5？
        QString giftName = data.value("giftName").toString();
        QString username = data.value("uname").toString();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble()); // 秒
        timestamp = QDateTime::currentSecsSinceEpoch(); // *不管送出礼物的时间，只管机器人接收到的时间
        QString coinType = data.value("coin_type").toString();
        int totalCoin = data.value("total_coin").toInt();

        qDebug() << s8("接收到送礼：") << username << giftId << giftName << num << s8("  总价值：") << totalCoin << coinType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        if (!data.value("medal_info").isNull())
        {
            QJsonObject medalInfo = data.value("medal_info").toObject();
            QString anchorRoomId = snum(qint64(medalInfo.value("anchor_room_id").toDouble())); // !注意：这个一直为0！
            QString anchorUname = medalInfo.value("anchor_uname").toString(); // !注意：也是空的
            int guardLevel = medalInfo.value("guard_level").toInt();
            int isLighted = medalInfo.value("is_lighted").toInt();
            int medalColor = medalInfo.value("medal_color").toInt();
            int medalColorBorder = medalInfo.value("medal_color_border").toInt();
            int medalColorEnd = medalInfo.value("medal_color_end").toInt();
            int medalColorStart = medalInfo.value("medal_color_start").toInt();
            int medalLevel = medalInfo.value("medal_level").toInt();
            QString medalName = medalInfo.value("medal_name").toString();
            QString spacial = medalInfo.value("special").toString();
            QString targetId = snum(qint64(medalInfo.value("target_id").toDouble())); // 目标用户ID
            if (!medalName.isEmpty())
            {
                QString cs = QString::number(medalColor, 16);
                while (cs.size() < 6)
                    cs = "0" + cs;
                danmaku.setMedal(anchorRoomId, medalName, medalLevel, cs, anchorUname);
            }
        }

        bool merged = mergeGiftCombo(danmaku); // 如果有合并，则合并到之前的弹幕上面
        danmaku.setFirst(merged ? 0 : 1);
        if (!merged)
        {
            appendNewLiveDanmaku(danmaku);
        }
        if (ui->saveEveryGiftCheck->isChecked())
            saveEveryGift(danmaku);

        if (!justStart && ui->autoSendGiftCheck->isChecked()) // 是否需要礼物答谢
        {
            QJsonValue batchComboIdVal = data.value("batch_combo_id");
            QString batchComboId = batchComboIdVal.toString();
            if (!ui->giftComboSendCheck->isChecked() || batchComboIdVal.isNull()) // 立刻发送
            {
                // 如果合并了，那么可能已经感谢了，就不用管了
                if (!merged)
                {
                    QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
                    if (words.size())
                    {
                        int r = qrand() % words.size();
                        QString msg = words.at(r);
                        if (strongNotifyUsers.contains(uid))
                        {
                            if (debugPrint)
                                localNotify("[强提醒]");
                            sendCdMsg(msg, NOTIFY_CD, GIFT_CD_CN,
                                      ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked());
                        }
                        else
                            sendGiftMsg(msg);
                    }
                    else if (debugPrint)
                    {
                        localNotify("[没有可发送的礼物答谢弹幕]");
                    }
                }
                else if (debugPrint)
                {
                    localNotify("[礼物被合并，不答谢]");
                }
            }
            else // 延迟发送
            {
                if (giftCombos.contains(batchComboId)) // 已经连击了，合并
                {
                    giftCombos[batchComboId].addGift(num, totalCoin, QDateTime::currentDateTime());
                }
                else // 创建新的连击
                {
                    danmaku.setTime(QDateTime::currentDateTime());
                    giftCombos.insert(batchComboId, danmaku);
                    if (!comboTimer->isActive())
                        comboTimer->start();
                }
            }
        }

        if (coinType == "silver")
        {
            qint64 userSilver = danmakuCounts->value("silver/" + snum(uid)).toLongLong();
            userSilver += totalCoin;
            danmakuCounts->setValue("silver/"+snum(uid), userSilver);

            dailyGiftSilver += totalCoin;
            if (dailySettings)
                dailySettings->setValue("gift_silver", dailyGiftSilver);
        }
        if (coinType == "gold")
        {
            qint64 userGold = danmakuCounts->value("gold/" + snum(uid)).toLongLong();
            userGold += totalCoin;
            danmakuCounts->setValue("gold/"+snum(uid), userGold);

            dailyGiftGold += totalCoin;
            if (dailySettings)
                dailySettings->setValue("gift_gold", dailyGiftGold);

            // 正在PK，保存弹幕历史
            // 因为最后的大乱斗最佳助攻只提供名字，所以这里需要保存 uname->uid 的映射
            // 方便起见，直接全部保存下来了
            pkGifts.append(danmaku);

            // 添加礼物记录
            appendLiveGift(danmaku);

            // 正在偷塔阶段
            if (pkEnding && uid == cookieUid.toLongLong()) // 机器人账号
            {
//                pkVoting -= totalCoin;
//                if (pkVoting < 0) // 自己用其他设备送了更大的礼物
//                {
//                    pkVoting = 0;
//                }
            }
        }

        // 都送礼了，总该不是机器人了吧
        markNotRobot(uid);

        // 监听勋章升级
        if (ui->listenMedalUpgradeCheck->isChecked())
        {
            detectMedalUpgrade(danmaku);
        }

        triggerCmdEvent(cmd, danmaku);
    }
    else if (cmd == "COMBO_SEND") // 连击礼物
    {
        /*{
            "cmd": "COMBO_SEND",
            "data": {
                "action": "投喂",
                "batch_combo_id": "batch:gift:combo_id:8833188:354580019:30607:1610168283.0188",
                "batch_combo_num": 9,
                "combo_id": "gift:combo_id:8833188:354580019:30607:1610168283.0182",
                "combo_num": 9,
                "combo_total_coin": 0,
                "gift_id": 30607,
                "gift_name": "小心心",
                "gift_num": 0,
                "is_show": 1,
                "medal_info": {
                    "anchor_roomid": 0,
                    "anchor_uname": "",
                    "guard_level": 0,
                    "icon_id": 0,
                    "is_lighted": 1,
                    "medal_color": 1725515,
                    "medal_color_border": 1725515,
                    "medal_color_end": 5414290,
                    "medal_color_start": 1725515,
                    "medal_level": 23,
                    "medal_name": "好听",
                    "special": "",
                    "target_id": 354580019
                },
                "name_color": "",
                "r_uname": "薄薄的温酱",
                "ruid": 354580019,
                "send_master": null,
                "total_num": 9,
                "uid": 8833188,
                "uname": "南酱的可露儿"
            }
        }*/
        /*{
            "cmd": "COMBO_SEND",
            "data": {
                "uid": 20285041,
                "ruid": 2070473390,
                "uname": "懒一夕智能科技",
                "r_uname": "喵大王_cat",
                "combo_num": 4,
                "gift_id": 30607,
                "gift_num": 0,
                "batch_combo_num": 4,
                "gift_name": "小心心",
                "action": "投喂",
                "combo_id": "gift:combo_id:20285041:2070473390:30607:1614439816.1648",
                "batch_combo_id": "batch:gift:combo_id:20285041:2070473390:30607:1614439816.1655",
                "is_show": 1,
                "send_master": null,
                "name_color": "",
                "total_num": 4,
                "medal_info": {
                    "target_id": 0,
                    "special": "",
                    "icon_id": 0,
                    "anchor_uname": "",
                    "anchor_roomid": 0,
                    "medal_level": 0,
                    "medal_name": "",
                    "medal_color": 0,
                    "medal_color_start": 0,
                    "medal_color_end": 0,
                    "medal_color_border": 0,
                    "is_lighted": 0,
                    "guard_level": 0
                },
                "combo_total_coin": 0
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "SUPER_CHAT_MESSAGE") // 醒目留言
    {
        /*{
            "cmd": "SUPER_CHAT_MESSAGE",
            "data": {
                "background_bottom_color": "#2A60B2",
                "background_color": "#EDF5FF",
                "background_color_end": "#405D85",
                "background_color_start": "#3171D2",
                "background_icon": "",
                "background_image": "https://i0.hdslb.com/bfs/live/a712efa5c6ebc67bafbe8352d3e74b820a00c13e.png",
                "background_price_color": "#7497CD",
                "color_point": 0.7,
                "end_time": 1613125905,
                "gift": {
                    "gift_id": 12000,
                    "gift_name": "醒目留言", // 这个是特殊礼物？
                    "num": 1
                },
                "id": 1278390,
                "is_ranked": 0,
                "is_send_audit": "0",
                "medal_info": {
                    "anchor_roomid": 1010,
                    "anchor_uname": "KB呆又呆",
                    "guard_level": 3,
                    "icon_id": 0,
                    "is_lighted": 1,
                    "medal_color": "#6154c",
                    "medal_color_border": 6809855,
                    "medal_color_end": 6850801,
                    "medal_color_start": 398668,
                    "medal_level": 25,
                    "medal_name": "KKZ",
                    "special": "",
                    "target_id": 389088
                },
                "message": "最右边可以爬上去",
                "message_font_color": "#A3F6FF",
                "message_trans": "",
                "price": 30,
                "rate": 1000,
                "start_time": 1613125845,
                "time": 60,
                "token": "767AB474",
                "trans_mark": 0,
                "ts": 1613125845,
                "uid": 35030958,
                "user_info": {
                    "face": "http://i0.hdslb.com/bfs/face/cdd8fbb13b2034dc3651096cbeef4b5e89765c35.jpg",
                    "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                    "guard_level": 3,
                    "is_main_vip": 1,
                    "is_svip": 0,
                    "is_vip": 0,
                    "level_color": "#61c05a",
                    "manager": 0,
                    "name_color": "#00D1F1",
                    "title": "0",
                    "uname": "么么么么么么么么句号",
                    "user_level": 14
                }
            },
            "roomid": "1010"
        }*/

        MyJson data = json.value("data").toObject();
        JL(data, uid);
        JS(data, message);
        JS(data, message_font_color);
        JL(data, end_time); // 秒
        JI(data, price); // 注意这个是价格（单位元），不是金瓜子，并且30起步

        JO(data, gift);
        JI(gift, gift_id);
        JS(gift, gift_name);
        JI(gift, num);

        JO(data, user_info);
        JS(user_info, uname);
        JI(user_info, user_level);
        JS(user_info, name_color);
        JI(user_info, is_main_vip);
        JI(user_info, is_vip);
        JI(user_info, is_svip);

        JO(data, medal_info);
        JL(medal_info, anchor_roomid);
        JS(medal_info, anchor_uname);
        JI(medal_info, guard_level);
        JI(medal_info, medal_level);
        JS(medal_info, medal_color);
        JS(medal_info, medal_name);
        JL(medal_info, target_id);

        if (gift_id != 12000) // 醒目留言
        {
            qWarning() << "非醒目留言的特殊聊天消息：" << json;
            return ;
        }

        LiveDanmaku danmaku(uname, message, uid, user_level, QDateTime::fromSecsSinceEpoch(end_time), name_color, message_font_color,
                    gift_id, gift_name, num, price);
        danmaku.setMedal(snum(anchor_roomid), medal_name, medal_level, medal_color, anchor_uname);
        appendNewLiveDanmaku(danmaku);

        pkGifts.append(danmaku);
        triggerCmdEvent(cmd, danmaku);
    }
    else if (cmd == "SUPER_CHAT_MESSAGE_JPN") // 醒目留言日文翻译
    {
        /*{
            "cmd": "SUPER_CHAT_MESSAGE_JPN",
            "data": {
                "background_bottom_color": "#2A60B2",
                "background_color": "#EDF5FF",
                "background_icon": "",
                "background_image": "https://i0.hdslb.com/bfs/live/a712efa5c6ebc67bafbe8352d3e74b820a00c13e.png",
                "background_price_color": "#7497CD",
                "end_time": 1613125905,
                "gift": {
                    "gift_id": 12000,
                    "gift_name": "醒目留言",
                    "num": 1
                },
                "id": "1278390",
                "is_ranked": 0,
                "medal_info": {
                    "anchor_roomid": 1010,
                    "anchor_uname": "KB呆又呆",
                    "icon_id": 0,
                    "medal_color": "#6154c",
                    "medal_level": 25,
                    "medal_name": "KKZ",
                    "special": "",
                    "target_id": 389088
                },
                "message": "最右边可以爬上去",
                "message_jpn": "",
                "price": 30,
                "rate": 1000,
                "start_time": 1613125845,
                "time": 60,
                "token": "767AB474",
                "ts": 1613125845,
                "uid": "35030958",
                "user_info": {
                    "face": "http://i0.hdslb.com/bfs/face/cdd8fbb13b2034dc3651096cbeef4b5e89765c35.jpg",
                    "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                    "guard_level": 3,
                    "is_main_vip": 1,
                    "is_svip": 0,
                    "is_vip": 0,
                    "level_color": "#61c05a",
                    "manager": 0,
                    "title": "0",
                    "uname": "么么么么么么么么句号",
                    "user_level": 14
                }
            },
            "roomid": "1010"
        }*/
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "SUPER_CHAT_MESSAGE_DELETE") // 删除醒目留言
    {
        qDebug() << "删除醒目留言：" << json;
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "SPECIAL_GIFT") // 节奏风暴（特殊礼物？）
    {
        /*{
            "cmd": "SPECIAL_GIFT",
            "data": {
                "39": {
                    "action": "start",
                    "content": "糟了，是心动的感觉！",
                    "hadJoin": 0,
                    "id": "3072200788973",
                    "num": 1,
                    "storm_gif": "http://static.hdslb.com/live-static/live-room/images/gift-section/mobilegift/2/jiezou.gif?2017011901",
                    "time": 90
                }
            }
        }*/

        QJsonObject data = json.value("data").toObject();
        QJsonObject sg = data.value("39").toObject();
        QString text = sg.value("content").toString();
        qint64 id = qint64(sg.value("id").toDouble());
        int time = sg.value("time").toInt();
        if (danmakuWindow)
        {
            danmakuWindow->addBlockText(text);
            QTimer::singleShot(time * 1000, danmakuWindow, [=]{
                danmakuWindow->removeBlockText(text);
            });
        }
        qDebug() << "节奏风暴：" << text << ui->autoLOTCheck->isChecked();
        if (ui->autoLOTCheck->isChecked())
        {
            joinStorm(id);
        }

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "WELCOME_GUARD") // 舰长进入（不会触发），通过guard_level=1/2/3分辨总督/提督/舰长
    {
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("username").toString();
        qint64 startTime = static_cast<qint64>(data.value("start_time").toDouble());
        qint64 endTime = static_cast<qint64>(data.value("end_time").toDouble());
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        QString unameColor = data.value("uname_color").toString();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        qDebug() << s8("舰长进入：") << username;
        /*QString localName = danmakuWindow->getLocalNickname(uid);
        if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(LiveDanmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp)
                                        , true, unameColor, spreadDesc, spreadInfo));
        appendNewLiveDanmaku(danmaku);

        triggerCmdEvent(cmd, danmaku);
    }
    else  if (cmd == "ENTRY_EFFECT") // 舰长进入、高能榜（不知道到榜几）、姥爷的同时会出现
    {
        // 欢迎舰长
        /*{
            "cmd": "ENTRY_EFFECT",
            "data": {
                "id": 4,
                "uid": 20285041,
                "target_id": 688893202,
                "mock_effect": 0,
                "face": "https://i2.hdslb.com/bfs/face/24420cdcb6eeb119dbcd1f1843fdd8ada5b7d045.jpg",
                "privilege_type": 3,
                "copy_writing": "欢迎舰长 \\u003c%心乂鸽鸽%\\u003e 进入直播间",
                "copy_color": "#ffffff",
                "highlight_color": "#E6FF00",
                "priority": 70,
                "basemap_url": "https://i0.hdslb.com/bfs/live/mlive/f34c7441cdbad86f76edebf74e60b59d2958f6ad.png",
                "show_avatar": 1,
                "effective_time": 2,
                "web_basemap_url": "",
                "web_effective_time": 0,
                "web_effect_close": 0,
                "web_close_time": 0,
                "business": 1,
                "copy_writing_v2": "欢迎 \\u003c^icon^\\u003e 舰长 \\u003c%心乂鸽鸽%\\u003e 进入直播间",
                "icon_list": [
                    2
                ],
                "max_delay_time": 7
            }
        }*/

        // 欢迎姥爷
        /*{
            "cmd": "ENTRY_EFFECT",
            "data": {
                "basemap_url": "https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png",
                "business": 3,
                "copy_color": "#000000",
                "copy_writing": "欢迎 <%___君陌%> 进入直播间",
                "copy_writing_v2": "欢迎 <^icon^> <%___君陌%> 进入直播间",
                "effective_time": 2,
                "face": "https://i1.hdslb.com/bfs/face/8fb8336e1ae50001ca76b80c30b01d23b07203c9.jpg",
                "highlight_color": "#FFF100",
                "icon_list": [
                    2
                ],
                "id": 136,
                "max_delay_time": 7,
                "mock_effect": 0,
                "priority": 1,
                "privilege_type": 0,
                "show_avatar": 1,
                "target_id": 5988102,
                "uid": 453364,
                "web_basemap_url": "https://i0.hdslb.com/bfs/live/mlive/586f12135b6002c522329904cf623d3f13c12d2c.png",
                "web_close_time": 900,
                "web_effect_close": 0,
                "web_effective_time": 2
            }
        }*/
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString copy_writing = data.value("copy_writing").toString();
        QStringList results = QRegularExpression("欢迎(舰长|提督|总督)?.+?<%(.+)%>").match(copy_writing).capturedTexts();
        LiveDanmaku danmaku;
        if (results.size() < 2 || results.at(1).isEmpty()) // 不是船员
        {
            qDebug() << ">>>>>>高能榜进入：" << copy_writing;
            QStringList results = QRegularExpression("^欢迎\\s*<%(.+)%>").match(copy_writing).capturedTexts();
            if (results.size() < 2)
            {
                qWarning() << "识别舰长进入失败：" << copy_writing;
                qWarning() << data;
                return ;
            }

            // 高能榜上的用户
            for (int i = 0; i < onlineGoldRank.size(); i++)
            {
                if (onlineGoldRank.at(i).getUid() == uid)
                {
                    danmaku = onlineGoldRank.at(i); // 就是这个用户了
                    danmaku.setTime(QDateTime::currentDateTime());
                    qDebug() << "                guard:" << danmaku.getGuard();
                    break;
                }
            }
            if (danmaku.getUid() == 0)
            {
                qWarning() << "未在已有高能榜上找到，立即更新高能榜";
                updateOnlineGoldRank();
                return ;
            }

            // 高能榜的，不能有guard，不然会误判为牌子
            // danmaku.setGuardLevel(0);
        }
        else // 舰长进入
        {
            qDebug() << ">>>>>>舰长进入：" << copy_writing;

            QString gd = results.at(1);
            QString uname = results.at(2); // 这个昵称会被系统自动省略（太长后面会是两个点）
            if (currentGuards.contains(uid))
                uname = currentGuards[uid];
            int guardLevel = 0;
            if (gd == "总督")
                guardLevel = 1;
            else if (gd == "提督")
                guardLevel = 2;
            else if (gd == "舰长")
                guardLevel = 3;

            danmaku = LiveDanmaku(guardLevel, uname, uid, QDateTime::currentDateTime());
        }

        userComeEvent(danmaku);
        triggerCmdEvent(cmd, danmaku);
    }
    else if (cmd == "WELCOME") // 欢迎老爷，通过vip和svip区分月费和年费老爷
    {
        QJsonObject data = json.value("data").toObject();
        qDebug() << data;
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        bool isAdmin = data.value("isAdmin").toBool();
        qDebug() << s8("欢迎观众：") << username << isAdmin;

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "INTERACT_WORD")
    {
        /* {
            "cmd": "INTERACT_WORD",
            "data": {
                "contribution": {
                    "grade": 0
                },
                "fans_medal": {
                    "anchor_roomid": 0,
                    "guard_level": 0,
                    "icon_id": 0,
                    "is_lighted": 0,
                    "medal_color": 0,
                    "medal_color_border": 12632256,
                    "medal_color_end": 12632256,
                    "medal_color_start": 12632256,
                    "medal_level": 0,
                    "medal_name": "",
                    "score": 0,
                    "special": "",
                    "target_id": 0
                },
                "identities": [
                    1
                ],
                "is_spread": 0,
                "msg_type": 4,
                "roomid": 22639465,
                "score": 1617974941375,
                "spread_desc": "",
                "spread_info": "",
                "tail_icon": 0,
                "timestamp": 1617974941,
                "uid": 20285041,
                "uname": "懒一夕智能科技",
                "uname_color": ""
            }
        } */

        QJsonObject data = json.value("data").toObject();
        int msgType = data.value("msg_type").toInt(); // 1进入直播间，2关注，3分享直播间，4特别关注
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        bool isadmin = data.value("isadmin").toBool(); // 一般房管都是舰长吧？
        QString unameColor = data.value("uname_color").toString();
        bool isSpread = data.value("is_spread").toBool();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        QJsonObject fansMedal = data.value("fans_medal").toObject();
        QString roomId = snum(qint64(data.value("room_id").toDouble()));

        qDebug() << s8("观众交互：") << username << msgType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp), isadmin,
                            unameColor, spreadDesc, spreadInfo);
        danmaku.setMedal(snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())),
                         fansMedal.value("medal_name").toString(),
                         fansMedal.value("medal_level").toInt(),
                         QString("#%1").arg(fansMedal.value("medal_color").toInt(), 6, 16, QLatin1Char('0')),
                         "");

        bool opposite = pking &&
                ((oppositeAudience.contains(uid) && !myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() &&
                     snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())) == pkRoomId));
        danmaku.setOpposite(opposite);

        if (roomId != "0" && roomId != this->roomId) // 关注对面主播，也会引发关注事件
        {
            qDebug() << "不是本房间，已忽略：" << roomId << "!=" << this->roomId;
            return ;
        }

        if (msgType == 1) // 进入直播间
        {
            userComeEvent(danmaku);

            triggerCmdEvent(cmd, danmaku);
        }
        else if (msgType == 2) // 2关注 4特别关注
        {
            danmaku.transToAttention(timestamp);
            appendNewLiveDanmaku(danmaku);

            if (!justStart && ui->autoSendAttentionCheck->isChecked())
            {
                sendAttentionThankIfNotRobot(danmaku);
            }
            else
            {
                judgeRobotAndMark(danmaku);
            }

            triggerCmdEvent("ATTENTION", danmaku); // !这个是单独修改的
        }
        else if (msgType == 3) // 分享直播间
        {
            danmaku.transToShare();
            localNotify(username + "分享了直播间", uid);

            triggerCmdEvent("SHARE", danmaku);
        }
        else if (msgType == 4) // 特别关注
        {
            danmaku.transToAttention(timestamp);
            danmaku.setSpecial(1);
            appendNewLiveDanmaku(danmaku);

            if (!justStart && ui->autoSendAttentionCheck->isChecked())
            {
                sendAttentionThankIfNotRobot(danmaku);
            }
            else
            {
                judgeRobotAndMark(danmaku);
            }

            triggerCmdEvent("SPECIAL_ATTENTION", danmaku); // !这个是单独修改的
        }
        else
        {
            qDebug() << "~~~~~~~~~~~~~~~~~~~~~~~~新的进入msgType" << msgType << json;
        }
    }
    else if (cmd == "ROOM_BLOCK_MSG") // 被禁言
    {
        QString nickname = json.value("uname").toString();
        qint64 uid = static_cast<qint64>(json.value("uid").toDouble());
        LiveDanmaku danmaku(LiveDanmaku(nickname, uid));
        appendNewLiveDanmaku(danmaku);
        blockedQueue.append(danmaku);

        triggerCmdEvent(cmd, danmaku);
    }
    else if (handlePK(json))
    {
    }
    else if (handlePK2(json)) // 太多了，换到单独一个方法里面
    {
    }
    else if (cmd == "GUARD_BUY") // 有人上舰
    {
        // {"end_time":1611343771,"gift_id":10003,"gift_name":"舰长","guard_level":3,"num":1,"price":198000,"start_time":1611343771,"uid":67756641,"username":"31119657605_bili"}
        QJsonObject data = json.value("data").toObject();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("username").toString();
        QString giftName = data.value("gift_name").toString();
        int price = data.value("price").toInt();
        int gift_id = data.value("guard_id").toInt();
        int guard_level = data.value("guard_level").toInt();
        int num = data.value("num").toInt();
        // start_time和end_time都是当前时间？
        int guardCount = danmakuCounts->value("guard/" + snum(uid), 0).toInt();
        qDebug() << username << s8("购买") << giftName << num << guardCount;
        LiveDanmaku danmaku(username, uid, giftName, num, guard_level, gift_id, price,
                            guardCount == 0 ? 1 : currentGuards.contains(uid) ? 0 : 2);
        appendNewLiveDanmaku(danmaku);
        appendLiveGuard(danmaku);

        if (ui->saveEveryGuardCheck->isChecked())
            saveEveryGuard(danmaku);

        if (!guardCount)
        {
            triggerCmdEvent("FIRST_GUARD", danmaku);
        }
        currentGuards[uid] = username;
        guardInfos.append(LiveDanmaku(guard_level, username, uid, QDateTime::currentDateTime()));

        if (!justStart && ui->autoSendGiftCheck->isChecked())
        {
            QStringList words = getEditConditionStringList(ui->autoThankWordsEdit->toPlainText(), danmaku);
            if (words.size())
            {
                int r = qrand() % words.size();
                QString msg = words.at(r);
                sendCdMsg(msg, NOTIFY_CD, NOTIFY_CD_CN,
                          ui->sendGiftTextCheck->isChecked(), ui->sendGiftVoiceCheck->isChecked());
            }
            else if (debugPrint)
            {
                localNotify("[没有可发送的上船弹幕]");
            }
        }

        qint64 userGold = danmakuCounts->value("gold/" + snum(uid)).toLongLong();
        userGold += price;
        danmakuCounts->setValue("gold/"+snum(uid), userGold);

        int addition = 1;
        if (giftName == "舰长")
            addition = 1;
        else if (giftName == "提督")
            addition = 10;
        else if (giftName == "总督")
            addition = 100;
        guardCount += addition;
        danmakuCounts->setValue("guard/" + snum(uid), guardCount);

        dailyGuard += num;
        if (dailySettings)
            dailySettings->setValue("guard", dailyGuard);

        triggerCmdEvent(cmd, danmaku);
    }
    else if (cmd == "USER_TOAST_MSG") // 续费舰长会附带的；购买不知道
    {
        /*{
            "cmd": "USER_TOAST_MSG",
            "data": {
                "anchor_show": true,
                "color": "#00D1F1",
                "end_time": 1610167490,
                "guard_level": 3,
                "is_show": 0,
                "num": 1,
                "op_type": 3,
                "payflow_id": "2101091244192762134756573",
                "price": 138000,
                "role_name": "舰长",
                "start_time": 1610167490,
                "svga_block": 0,
                "target_guard_count": 128,
                "toast_msg": "<%分说的佛酱%> 自动续费了舰长",
                "uid": 480643475,
                "unit": "月",
                "user_show": true,
                "username": "分说的佛酱"
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ONLINE_RANK_V2") // 礼物榜（高能榜）更新
    {
        /*{
            "cmd": "ONLINE_RANK_V2",
            "data": {
                "list": [
                    {
                        "face": "http://i0.hdslb.com/bfs/face/5eae154b4ed09c8ae4017325f5fa1ed8fa3757a9.jpg",
                        "guard_level": 3,
                        "rank": 1,
                        "score": "1380",
                        "uid": 480643475,
                        "uname": "分说的佛酱"
                    },
                    {
                        "face": "http://i2.hdslb.com/bfs/face/65536122b97302b86b93847054d4ab8cc155afe3.jpg",
                        "guard_level": 3,
                        "rank": 2,
                        "score": "610",
                        "uid": 407543009,
                        "uname": "布可人"
                    },
                    ..........
                ],
                "rank_type": "gold-rank"
            }
        }*/
        QJsonArray array = json.value("data").toObject().value("list").toArray();
        // 因为高能榜上的只有名字和ID，没有粉丝牌，有需要的话还是需要手动刷新一下
        if (array.size() != onlineGoldRank.size())
        {
            updateOnlineGoldRank();
        }
        else
        {
            // 如果仅仅是排名和金瓜子，那么就挨个修改吧
            foreach (auto val, array)
            {
                auto user = val.toObject();
                qint64 uid = static_cast<qint64>(user.value("uid").toDouble());
                int score = user.value("score").toInt();
                int rank = user.value("rank").toInt();

                for (int i = 0; i < onlineGoldRank.size(); i++)
                {
                    if (onlineGoldRank.at(i).getUid() == uid)
                    {
                        onlineGoldRank[i].setFirst(rank);
                        onlineGoldRank[i].setTotalCoin(score);
                        break;
                    }
                }
            }
        }

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ONLINE_RANK_TOP3") // 高能榜前3变化（不知道会不会跟着 ONLINE_RANK_V2）
    {
        /*{
            "cmd": "ONLINE_RANK_TOP3",
            "data": {
                "list": [
                    {
                        "msg": "恭喜 <%分说的佛酱%> 成为高能榜",
                        "rank": 1
                    }
                ]
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ONLINE_RANK_COUNT") // 高能榜数量变化（但必定会跟着 ONLINE_RANK_V2）
    {
        /*{
            "cmd": "ONLINE_RANK_COUNT",
            "data": {
                "count": 9
            }
        }*/

        // 数量变化了，那还是得刷新一下
        updateOnlineGoldRank();

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "NOTICE_MSG") // 为什么压缩的消息还有一遍？
    {
        /*{
            "business_id": "",
            "cmd": "NOTICE_MSG",
            "full": {
                "background": "#FFB03CFF",
                "color": "#FFFFFFFF",
                "head_icon": "https://i0.hdslb.com/bfs/live/72337e86020b8d0874d817f15c48a610894b94ff.png",
                "head_icon_fa": "https://i0.hdslb.com/bfs/live/72337e86020b8d0874d817f15c48a610894b94ff.png",
                "head_icon_fan": 1,
                "highlight": "#B25AC1FF",
                "tail_icon": "https://i0.hdslb.com/bfs/live/822da481fdaba986d738db5d8fd469ffa95a8fa1.webp",
                "tail_icon_fa": "https://i0.hdslb.com/bfs/live/38cb2a9f1209b16c0f15162b0b553e3b28d9f16f.png",
                "tail_icon_fan": 4,
                "time": 10
            },
            "half": {
                "background": "",
                "color": "",
                "head_icon": "",
                "highlight": "",
                "tail_icon": "",
                "time": 0
            },
            "link_url": "",
            "msg_common": "",
            "msg_self": "<%分说的佛酱%> 自动续费了主播的 <%舰长%>",
            "msg_type": 3,
            "real_roomid": 12735949,
            "roomid": 12735949,
            "scatter": {
                "max": 0,
                "min": 0
            },
            "shield_uid": -1,
            "side": {
                "background": "#FFE9C8FF",
                "border": "#FFCFA4FF",
                "color": "#EF903AFF",
                "head_icon": "https://i0.hdslb.com/bfs/live/31566d8cd5d468c30de8c148c5d06b3b345d8333.png",
                "highlight": "#D54900FF"
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "SPECIAL_GIFT")
    {
        /*{
            "cmd": "SPECIAL_GIFT",
            "data": {
                "39": {
                    "action": "end",
                    "id": 3032328093737
                }
            }
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ANCHOR_LOT_CHECKSTATUS") //  开启天选前的审核，审核过了才是真正开启
    {
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ANCHOR_LOT_START") // 开启天选
    {
        /*{
            "cmd": "ANCHOR_LOT_START",
            "data": {
                "asset_icon": "https://i0.hdslb.com/bfs/live/992c2ccf88d3ea99620fb3a75e672e0abe850e9c.png",
                "award_image": "",
                "award_name": "5.2元红包",
                "award_num": 1,
                "cur_gift_num": 0,
                "current_time": 1610529938,
                "danmu": "娇娇赛高",
                "gift_id": 0,
                "gift_name": "",
                "gift_num": 1,
                "gift_price": 0,
                "goaway_time": 180,
                "goods_id": -99998,
                "id": 773667,
                "is_broadcast": 1,
                "join_type": 0,
                "lot_status": 0,
                "max_time": 600,
                "require_text": "关注主播",
                "require_type": 1, // 1是关注？
                "require_value": 0,
                "room_id": 22532956,
                "send_gift_ensure": 0,
                "show_panel": 1,
                "status": 1,
                "time": 599,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        }*/

        /*{
            "cmd": "ANCHOR_LOT_START",
            "data": {
                "asset_icon": "https://i0.hdslb.com/bfs/live/992c2ccf88d3ea99620fb3a75e672e0abe850e9c.png",
                "award_image": "",
                "award_name": "5.2元红包",
                "award_num": 1,
                "cur_gift_num": 0,
                "current_time": 1610535661,
                "danmu": "我就是天选之人！", // 送礼物的话也是需要发弹幕的（自动发送+自动送礼）
                "gift_id": 20008, // 冰阔落ID
                "gift_name": "冰阔落",
                "gift_num": 1,
                "gift_price": 1000,
                "goaway_time": 180,
                "goods_id": 15, // 物品ID？
                "id": 773836,
                "is_broadcast": 1,
                "join_type": 1,
                "lot_status": 0,
                "max_time": 600,
                "require_text": "无",
                "require_type": 0,
                "require_value": 0,
                "room_id": 22532956,
                "send_gift_ensure": 0,
                "show_panel": 1,
                "status": 1,
                "time": 599,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        }*/

        QJsonObject data = json.value("data").toObject();
        qint64 id = static_cast<qint64>(data.value("id").toDouble());
        QString danmu = data.value("danmu").toString();
        int giftId = data.value("gift_id").toInt();
        int time = data.value("time").toInt();
        qDebug() << "天选弹幕：" << danmu;
        if (!danmu.isEmpty() && giftId <= 0 && ui->autoLOTCheck->isChecked())
        {
            int requireType = data.value("require_type").toInt();
            joinLOT(id, requireType);
        }
        if (danmakuWindow)
        {
            danmakuWindow->addBlockText(danmu);
            QTimer::singleShot(time, danmakuWindow, [=]{
                danmakuWindow->removeBlockText(danmu);
            });
        }

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ANCHOR_LOT_END") // 天选结束
    {
        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "ANCHOR_LOT_AWARD") // 天选结果推送，在结束后的不到一秒左右
    {
        /* {
            "cmd": "ANCHOR_LOT_AWARD",
            "data": {
                "award_image": "",
                "award_name": "兔の自拍",
                "award_num": 1,
                "award_users": [
                    {
                        "color": 5805790,
                        "face": "http://i1.hdslb.com/bfs/face/f07a981e34985819367eb709baefd80ad5ab4746.jpg",
                        "level": 26,
                        "uid": 44916867,
                        "uname": "-领主-"
                    }
                ],
                "id": 1014952,
                "lot_status": 2,
                "url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html?is_live_half_webview=1&hybrid_biz=live-lottery-anchor&hybrid_half_ui=1,5,100p,100p,000000,0,30,0,0,1;2,5,100p,100p,000000,0,30,0,0,1;3,5,100p,100p,000000,0,30,0,0,1;4,5,100p,100p,000000,0,30,0,0,1;5,5,100p,100p,000000,0,30,0,0,1;6,5,100p,100p,000000,0,30,0,0,1;7,5,100p,100p,000000,0,30,0,0,1;8,5,100p,100p,000000,0,30,0,0,1",
                "web_url": "https://live.bilibili.com/p/html/live-lottery/anchor-join.html"
            }
        } */

        QJsonObject data = json.value("data").toObject();
        QString awardName = data.value("award_name").toString();
        int awardNum = data.value("award_num").toInt();
        QJsonArray awardUsers = data.value("award_users").toArray();
        QStringList names;
        qint64 firstUid = 0;
        foreach (QJsonValue val, awardUsers)
        {
            QJsonObject user = val.toObject();
            QString uname = user.value("uname").toString();
            names.append(uname);
            if (!firstUid)
                firstUid = qint64(user.value("uid").toDouble());
        }

        QString awardRst = awardName + (awardNum > 1 ? "×" + snum(awardNum) : "");
        localNotify("[天选] " + names.join(",") + " 中奖：" + awardRst);

        triggerCmdEvent(cmd, LiveDanmaku(firstUid, names.join(","), awardRst));
    }
    else if (cmd == "VOICE_JOIN_ROOM_COUNT_INFO") // 等待连麦队列数量变化
    {
        /*{
            "cmd": "VOICE_JOIN_ROOM_COUNT_INFO",
            "data": {
                "apply_count": 1, // 猜测：1的话就是添加申请连麦，0是取消申请连麦
                "notify_count": 0,
                "red_point": 0,
                "room_id": 22532956,
                "room_status": 1,
                "root_status": 1
            },
            "roomid": 22532956
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "VOICE_JOIN_LIST") // 连麦申请、取消连麦申请；和VOICE_JOIN_ROOM_COUNT_INFO一起收到
    {
        /*{
            "cmd": "VOICE_JOIN_LIST",
            "data": {
                "apply_count": 1, // 等同于VOICE_JOIN_ROOM_COUNT_INFO的apply_count
                "category": 1,
                "red_point": 1,
                "refresh": 1,
                "room_id": 22532956
            },
            "roomid": 22532956
        }*/

        QJsonObject data = json.value("data").toObject();
        int point = data.value("red_point").toInt();
        localNotify("连麦队列：" + snum(point));

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "VOICE_JOIN_STATUS") // 连麦状态，连麦开始/结束
    {
        /*{
            "cmd": "VOICE_JOIN_STATUS",
            "data": {
                "channel": "voice320168",
                "channel_type": "voice",
                "current_time": 1610802781,
                "guard": 3,
                "head_pic": "http://i1.hdslb.com/bfs/face/5bbf173c5cf4f70481e5814e34bbdf6db564ef80.jpg",
                "room_id": 22532956,
                "start_at": 1610802781,
                "status": 1,                   // 1是开始连麦
                "uid": 1324369,
                "user_name":"\xE6\xB0\xB8\xE8\xBF\x9C\xE5\x8D\x95\xE6\x8E\xA8\xE5\xA8\x87\xE5\xA8\x87\xE7\x9A\x84\xE8\x82\x89\xE5\xA4\xB9\xE9\xA6\x8D",
                "web_share_link": "https://live.bilibili.com/h5/22532956"
            },
            "roomid": 22532956
        }*/
        /*{
            "cmd": "VOICE_JOIN_STATUS",
            "data": {
                "channel": "",
                "channel_type": "voice",
                "current_time": 1610802959,
                "guard": 0,
                "head_pic": "",
                "room_id": 22532956,
                "start_at": 0,
                "status": 0,                   // 0是取消连麦
                "uid": 0,
                "user_name": "",
                "web_share_link": "https://live.bilibili.com/h5/22532956"
            },
            "roomid": 22532956
        }*/

        QJsonObject data = json.value("data").toObject();
        int status = data.value("status").toInt();
        QString uname = data.value("username").toString();
        if (status) // 开始连麦
        {
            if (!uname.isEmpty())
                localNotify((uname + " 接入连麦"));
        }
        else // 取消连麦
        {
            if (!uname.isEmpty())
                localNotify(uname + " 结束连麦");
        }

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "WARNING") // 被警告
    {
        /*{
            "cmd": "WARNING",
            "msg":"违反直播分区规范，未摄像头露脸",
            "roomid": 22532956
        }*/
        QString msg = json.value("msg").toString();
        localNotify(msg);

        triggerCmdEvent(cmd, LiveDanmaku(msg));
    }
    else if (cmd == "room_admin_entrance")
    {
        /*{
            "cmd": "room_admin_entrance",
            "msg":"系统提示：你已被主播设为房管",
            "uid": 20285041
        }*/
        QString msg = json.value("msg").toString();
        qint64 uid = static_cast<qint64>(json.value("uid").toDouble());
        if (snum(uid) == cookieUid) // 不是自己的话，不用理会
        {
            localNotify(msg, uid);
            triggerCmdEvent(cmd, LiveDanmaku(msg));
        }
    }
    else if (cmd == "ROOM_ADMINS")
    {
        /*{
            "cmd": "ROOM_ADMINS",
            "uids": [
                36272011, 145884036, 10823381, 67756641, 35001804, 64494330, 295742464, 250439508, 90400995, 384733451, 632481, 41090958, 691018830, 33283612, 13381878, 1324369, 49912988, 2852700, 26472148, 415374297, 20285041
            ]
        }*/

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "LIVE_INTERACTIVE_GAME")
    {
        /*{
            "cmd": "LIVE_INTERACTIVE_GAME",
            "data": {
                "gift_name": "",
                "gift_num": 0,
                "msg": "哈哈哈哈哈哈哈哈哈",
                "price": 0,
                "type": 2,
                "uname": "每天都要学习混凝土"
            }
        }*/
    }
    else if (cmd == "CUT_OFF")
    {
        localNotify("直播间被超管切断");
    }
    else if (cmd == "STOP_LIVE_ROOM_LIST")
    {
        return ;
    }
    else
    {
        qWarning() << "未处理的命令：" << cmd << QString(QJsonDocument(json).toJson(QJsonDocument::Compact));
        triggerCmdEvent(cmd, LiveDanmaku());
    }
}

void BaseWindow::sendWelcomeIfNotRobot(LiveDanmaku danmaku)
{
    if (judgeRobot != 2)
    {
        sendWelcome(danmaku);
        return ;
    }

    judgeUserRobotByFans(danmaku, [=](LiveDanmaku danmaku){
        sendWelcome(danmaku);
    }, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void BaseWindow::sendAttentionThankIfNotRobot(LiveDanmaku danmaku)
{
    judgeUserRobotByFans(danmaku, [=](LiveDanmaku danmaku){
        sendAttentionThans(danmaku);
    }, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void BaseWindow::judgeUserRobotByFans(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    int val = robotRecord->value("robot/" + snum(danmaku.getUid()), 0).toInt();
    if (val == 1) // 是机器人
    {
        if (ifIs)
            ifIs(danmaku);
        return ;
    }
    else if (val == -1) // 是人
    {
        if (ifNot)
            ifNot(danmaku);
        return ;
    }
    else
    {
        if (danmaku.getMedalLevel() > 0 || danmaku.getLevel() > 1) // 使用等级判断
        {
            robotRecord->setValue("robot/" + snum(danmaku.getUid()), -1);
            if (ifNot)
                ifNot(danmaku);
            return ;
        }
    }

    // 网络判断
    QString url = "http://api.bilibili.com/x/relation/stat?vmid=" + snum(danmaku.getUid());
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int following = obj.value("following").toInt(); // 关注
        int follower = obj.value("follower").toInt(); // 粉丝
        // int whisper = obj.value("whisper").toInt(); // 悄悄关注（自己关注）
        // int black = obj.value("black").toInt(); // 黑名单（自己登录）
        bool robot =  (following >= 100 && follower <= 5) || (follower > 0 && following > follower * 100); // 机器人，或者小号
//        qDebug() << "判断机器人：" << danmaku.getNickname() << "    粉丝数：" << following << follower << robot;
        if (robot)
        {
            // 进一步使用投稿数量判断
            judgeUserRobotByUpstate(danmaku, ifNot, ifIs);
        }
        else // 不是机器人
        {
            robotRecord->setValue("robot/" + snum(danmaku.getUid()), -1);
            if (ifNot)
                ifNot(danmaku);
        }
    });
}

void BaseWindow::judgeUserRobotByUpstate(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    QString url = "http://api.bilibili.com/x/space/upstat?mid=" + snum(danmaku.getUid());
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int achive_view = obj.value("archive").toObject().value("view").toInt();
        int article_view = obj.value("article").toObject().value("view").toInt();
        int article_like = obj.value("article").toObject().value("like").toInt();
        bool robot = (achive_view + article_view + article_like < 10); // 机器人，或者小号
//        qDebug() << "判断机器人：" << danmaku.getNickname() << "    视频播放量：" << achive_view
//                 << "  专栏阅读量：" << article_view << "  专栏点赞数：" << article_like << robot;
        robotRecord->setValue("robot/" + snum(danmaku.getUid()), robot ? 1 : -1);
        if (robot)
        {
            if (ifIs)
                ifIs(danmaku);
        }
        else // 不是机器人
        {
            if (ifNot)
            {
                ifNot(danmaku);
            }
        }
    });
}

void BaseWindow::judgeUserRobotByUpload(LiveDanmaku danmaku, DanmakuFunc ifNot, DanmakuFunc ifIs)
{
    QString url = "http://api.vc.bilibili.com/link_draw/v1/doc/upload_count?uid=" + snum(danmaku.getUid());
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonObject obj = json.value("data").toObject();
        int allCount = obj.value("all_count").toInt();
        bool robot = (allCount <= 1); // 机器人，或者小号（1是因为有默认成为会员的相簿）
        qDebug() << "判断机器人：" << danmaku.getNickname() << "    投稿数量：" << allCount << robot;
        robotRecord->setValue("robot/" + snum(danmaku.getUid()), robot ? 1 : -1);
        if (robot)
        {
            if (ifIs)
                ifIs(danmaku);
        }
        else // 不是机器人
        {
            if (ifNot)
                ifNot(danmaku);
        }
    });
}

void BaseWindow::sendWelcome(LiveDanmaku danmaku)
{
    if (notWelcomeUsers.contains(danmaku.getUid())
            || (!ui->sendWelcomeTextCheck->isChecked()
            && !ui->sendWelcomeVoiceCheck->isChecked())) // 不自动欢迎
        return ;
    QStringList words = getEditConditionStringList(ui->autoWelcomeWordsEdit->toPlainText(), danmaku);
    if (!words.size())
    {
        if (debugPrint)
            localNotify("[没有可用的欢迎弹幕]");
        return ;
    }
    int r = qrand() % words.size();
    if (debugPrint && !(words.size() == 1 && words.first().trimmed().isEmpty()))
        localNotify("[rand " + snum(r) + " in " + snum(words.size()) + "]");
    QString msg = words.at(r);
    if (strongNotifyUsers.contains(danmaku.getUid()))
    {
        if (debugPrint)
            localNotify("[强提醒]");
        sendCdMsg(msg, 2000, NOTIFY_CD_CN,
                  ui->sendWelcomeTextCheck->isChecked(), ui->sendWelcomeVoiceCheck->isChecked());
    }
    else
    {
        sendCdMsg(msg, ui->sendWelcomeCDSpin->value() * 1000, WELCOME_CD_CN,
                  ui->sendWelcomeTextCheck->isChecked(), ui->sendWelcomeVoiceCheck->isChecked());
    }
}

void BaseWindow::sendAttentionThans(LiveDanmaku danmaku)
{
    QStringList words = getEditConditionStringList(ui->autoAttentionWordsEdit->toPlainText(), danmaku);
    if (!words.size())
    {
        if (debugPrint)
            localNotify("[没有可用的感谢关注弹幕]");
        return ;
    }
    int r = qrand() % words.size();
    QString msg = words.at(r);
    sendAttentionMsg(msg);
}

void BaseWindow::judgeRobotAndMark(LiveDanmaku danmaku)
{
    if (!judgeRobot)
        return ;
    judgeUserRobotByFans(danmaku, nullptr, [=](LiveDanmaku danmaku){
        // 实时弹幕显示机器人
        if (danmakuWindow)
            danmakuWindow->markRobot(danmaku.getUid());
    });
}

void BaseWindow::markNotRobot(qint64 uid)
{
    if (!judgeRobot)
        return ;
    int val = robotRecord->value("robot/" + snum(uid), 0).toInt();
    if (val != -1)
        robotRecord->setValue("robot/" + snum(uid), -1);
}

void BaseWindow::initTTS()
{
    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (!tts)
        {
            qDebug() << "初始化TTS语音模块";
            tts = new QTextToSpeech(this);
            tts->setRate( (voiceSpeed = settings->value("voice/speed", 50).toInt() - 50) / 50.0 );
            tts->setPitch( (voicePitch = settings->value("voice/pitch", 50).toInt() - 50) / 50.0 );
            tts->setVolume( (voiceVolume = settings->value("voice/volume", 50).toInt()) / 100.0 );
        }
#endif
        break;
    case VoiceXfy:
        if (!xfyTTS)
        {
            qDebug() << "初始化讯飞语音模块";
            xfyTTS = new XfyTTS(dataPath,
                                settings->value("xfytts/appid").toString(),
                                settings->value("xfytts/apikey").toString(),
                                settings->value("xfytts/apisecret").toString(),
                                this);
            ui->xfyAppIdEdit->setText(settings->value("xfytts/appid").toString());
            ui->xfyApiKeyEdit->setText(settings->value("xfytts/apikey").toString());
            ui->xfyApiSecretEdit->setText(settings->value("xfytts/apisecret").toString());
            xfyTTS->setName( voiceName = settings->value("xfytts/name", "xiaoyan").toString() );
            xfyTTS->setPitch( voicePitch = settings->value("voice/pitch", 50).toInt() );
            xfyTTS->setSpeed( voiceSpeed = settings->value("voice/speed", 50).toInt() );
            xfyTTS->setVolume( voiceSpeed = settings->value("voice/speed", 50).toInt() );
        }
        break;
    case VoiceCustom:
        break;
    }
}

void BaseWindow::speekVariantText(QString text)
{
    // 开始播放
    speakText(text);
}

void BaseWindow::speakText(QString text)
{
    // 处理特殊字符
    text.replace("_", " ");

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (!tts)
            initTTS();
        else if (tts->state() != QTextToSpeech::Ready)
            return ;
        tts->say(text);
#endif
        break;
    case VoiceXfy:
        if (!xfyTTS)
            initTTS();
        xfyTTS->speakText(text);
        break;
    case VoiceCustom:
        downloadAndSpeak(text);
        break;
    }
}

void BaseWindow::downloadAndSpeak(QString text)
{
    QString url = ui->voiceCustomUrlEdit->text();
    if (url.isEmpty())
        return ;
    url = url.replace("%1", text);
    const QString filePath = dataPath + "audios";
    QDir dir(filePath);
    dir.mkpath(filePath);

    get(url, [=](QNetworkReply* reply1){
        QByteArray fileData = reply1->readAll();
        if (fileData.isEmpty())
        {
            qWarning() << "网络音频为空";
            return ;
        }

        // 保存文件
        QString path = filePath + "/" + snum(QDateTime::currentMSecsSinceEpoch()) + ".mp3";
        QFile file(path);
        file.open(QFile::WriteOnly);
        QDataStream stream(&file);
        stream << fileData;
        file.flush();
        file.close();

        // 播放文件
        QMediaPlayer *player = new QMediaPlayer(this);
        player->setMedia(QUrl::fromLocalFile(path));
        connect(player, &QMediaPlayer::stateChanged, this, [=](QMediaPlayer::State state){
            if (state == QMediaPlayer::StoppedState)
            {
                player->deleteLater();

                QFile file(path);
                file.remove();
            }
        });
        player->play();
    });
}

void BaseWindow::showScreenDanmaku(LiveDanmaku danmaku)
{
    if (!ui->enableScreenDanmakuCheck->isChecked()) // 不显示所有弹幕
        return ;
    if (!ui->enableScreenMsgCheck->isChecked() && danmaku.getMsgType() != MSG_DANMAKU) // 不显示所有msg
        return ;
    if (danmaku.isPkLink()) // 对面同步过来的弹幕
        return ;

    // 显示动画
    QLabel* label = new QLabel(nullptr);
    label->setWindowFlag(Qt::FramelessWindowHint, true);
    label->setWindowFlag(Qt::Tool, true);
    label->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    label->setAttribute(Qt::WA_TranslucentBackground, true); // 设置窗口透明
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    if (danmaku.getMsgType() == MSG_DANMAKU || !danmaku.getText().isEmpty())
        label->setText(danmaku.getText());
    else
    {
        QString text = danmaku.toString();
        QRegularExpression re("^\\d+:\\d+:\\d+\\s+(.+)$"); // 简化表达式
        QRegularExpressionMatch match;
        if (text.indexOf(re, 0, &match) > -1)
            text = match.capturedTexts().at(1);
        label->setText(text);
    }
    label->setFont(screenDanmakuFont);

    auto isBlankColor = [=](QString c) -> bool {
        c = c.toLower();
        return c.isEmpty() || c == "#ffffff" || c == "#000000"
                || c == "#ffffffff" || c == "#00000000";
    };
    if (!danmaku.getTextColor().isEmpty() && !isBlankColor(danmaku.getTextColor()))
        label->setStyleSheet("color:" + danmaku.getTextColor() + ";");
    else
        label->setStyleSheet("color:" + QVariant(screenDanmakuColor).toString() + ";");
    label->adjustSize();
    label->show();

    QRect rect = getScreenRect();
    int left = rect.left() + rect.width() * ui->screenDanmakuLeftSpin->value() / 100;
    int right = rect.right() - rect.width() * ui->screenDanmakuRightSpin->value() / 100;
    int top = rect.top() + rect.height() * ui->screenDanmakuTopSpin->value() / 100;
    int bottom = rect.bottom() - rect.height() * ui->screenDanmakuBottomSpin->value() / 100;
    int sx = right, ex = left - label->width();
    if (left > right) // 从左往右
    {
        sx = right - label->width();
        ex = left;
    }

    // 避免重叠
    auto isOverlap = [=](int x, int y, int height){
        foreach (QLabel* label, screenLabels)
        {
            if (label->geometry().contains(x, y)
                    || label->geometry().contains(x, y+height))
                return true;
        }
        return false;
    };
    int forCount = 0;
    int y = 0;
    do {
        int ry = qrand() % (qMax(30, qAbs(bottom - top) - label->height()));
        y = qMin(top, bottom) + label->height() + ry;
        if (++forCount >= 32) // 最多循环32遍（如果弹幕极多，可能会必定叠在一起）
            break;
    } while (isOverlap(sx, y, label->height()));

    label->move(QPoint(sx, y));
    screenLabels.append(label);
    QPropertyAnimation* ani = new QPropertyAnimation(label, "pos");
    ani->setStartValue(QPoint(sx, y));
    ani->setEndValue(QPoint(ex, y));
    ani->setDuration(ui->screenDanmakuSpeedSpin->value() * 1000);
    connect(ani, &QPropertyAnimation::finished, ani, [=]{
        ani->deleteLater();
        screenLabels.removeOne(label);
        label->deleteLater();
    });
    ani->start();
}

/**
 * 合并消息
 * 在添加到消息队列前调用此函数判断
 * 若是，则同步合并实时弹幕中的礼物连击
 */
bool BaseWindow::mergeGiftCombo(LiveDanmaku danmaku)
{
    if (danmaku.getMsgType() != MSG_GIFT)
        return false;

    // 判断，同人 && 礼物同名 && x秒内
    qint64 uid = danmaku.getUid();
    int giftId = danmaku.getGiftId();
    qint64 time = danmaku.getTimeline().toSecsSinceEpoch();
    LiveDanmaku* merged = nullptr;
    int delayTime = ui->giftComboDelaySpin->value(); // + 1; // 多出的1秒当做网络延迟了

    // 遍历房间弹幕
    for (int i = roomDanmakus.size()-1; i >= 0; i--)
    {
        LiveDanmaku dm = roomDanmakus.at(i);
        qint64 t = dm.getTimeline().toSecsSinceEpoch();
        if (t == 0) // 有些是没带时间的
            continue;
        if (t + delayTime < time) // x秒以内
            return false;
        if (dm.getMsgType() != MSG_GIFT
                || dm.getUid() != uid
                || dm.getGiftId() != giftId)
            continue;

        // 是这个没错了
        merged = &roomDanmakus[i];
        break;
    }
    if (!merged)
        return false;

    // 开始合并
    qDebug() << "合并相同礼物至：" << merged->toString();
    merged->addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());

    // 合并实时弹幕
    if (danmakuWindow)
        danmakuWindow->mergeGift(danmaku, delayTime);

    return true;
}

bool BaseWindow::handlePK(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();

    if (cmd == "PK_BATTLE_START") // 开始大乱斗
    {
        pkStart(json);
    }
    else if (cmd == "PK_BATTLE_START_NEW")
    {

    }
    else if (cmd == "PK_BATTLE_PROCESS") // 双方送礼信息
    {
        pkProcess(json);
    }
    else if (cmd == "PK_BATTLE_END") // 结束信息
    {
        pkEnd(json);
    }
    else if (cmd == "PK_BATTLE_SETTLE_USER")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_USER",
            "data": {
                "battle_type": 0,
                "level_info": {
                    "first_rank_img": "https://i0.hdslb.com/bfs/live/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                    "first_rank_name": "白银斗士",
                    "second_rank_icon": "https://i0.hdslb.com/bfs/live/1f8c2a959f92592407514a1afeb705ddc55429cd.png",
                    "second_rank_num": 1
                },
                "my_info": {
                    "best_user": {
                        "award_info": null,
                        "award_info_list": [],
                        "badge": {
                            "desc": "",
                            "position": 0,
                            "url": ""
                        },
                        "end_win_award_info_list": {
                            "list": []
                        },
                        "exp": {
                            "color": 6406234,
                            "level": 1
                        },
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                        "pk_votes": 10,
                        "pk_votes_name": "乱斗值",
                        "uid": 64494330,
                        "uname": "我今天超可爱0"
                    },
                    "exp": {
                        "color": 9868950,
                        "master_level": {
                            "color": 5805790,
                            "level": 17
                        },
                        "user_level": 2
                    },
                    "face": "http://i2.hdslb.com/bfs/face/5b96b6ba5b078001e8159406710a8326d67cee5c.jpg",
                    "face_frame": "https://i0.hdslb.com/bfs/vc/d186b7d67d39e0894ebcc7f3ca5b35b3b56d5192.png",
                    "room_id": 22532956,
                    "uid": 688893202,
                    "uname": "娇娇子er"
                },
                "pk_id": "100729259",
                "result_info": {
                    "pk_crit_score": -1,
                    "pk_done_times": 17,
                    "pk_extra_score": 0,
                    "pk_extra_score_slot": "",
                    "pk_extra_value": 0,
                    "pk_resist_crit_score": -1,
                    "pk_task_score": 0,
                    "pk_times_score": 0,
                    "pk_total_times": -1,
                    "pk_votes": 10,
                    "pk_votes_name": "乱斗值",
                    "result_type_score": 12,
                    "task_score_list": [],
                    "total_score": 12,
                    "win_count": 2,
                    "win_final_hit": -1,
                    "winner_count_score": 0
                },
                "result_type": "2",
                "season_id": 29,
                "settle_status": 1,
                "winner": {
                    "best_user": {
                        "award_info": null,
                        "award_info_list": [],
                        "badge": {
                            "desc": "",
                            "position": 0,
                            "url": ""
                        },
                        "end_win_award_info_list": {
                            "list": []
                        },
                        "exp": {
                            "color": 6406234,
                            "level": 1
                        },
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "face_frame": "http://i0.hdslb.com/bfs/live/78e8a800e97403f1137c0c1b5029648c390be390.png",
                        "pk_votes": 10,
                        "pk_votes_name": "乱斗值",
                        "uid": 64494330,
                        "uname": "我今天超可爱0"
                    },
                    "exp": {
                        "color": 9868950,
                        "master_level": {
                            "color": 5805790,
                            "level": 17
                        },
                        "user_level": 2
                    },
                    "face": "http://i2.hdslb.com/bfs/face/5b96b6ba5b078001e8159406710a8326d67cee5c.jpg",
                    "face_frame": "https://i0.hdslb.com/bfs/vc/d186b7d67d39e0894ebcc7f3ca5b35b3b56d5192.png",
                    "room_id": 22532956,
                    "uid": 688893202,
                    "uname": "娇娇子er"
                }
            },
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006
        }*/
    }
    else if (cmd == "PK_BATTLE_SETTLE_V2")
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE_V2",
            "data": {
                "assist_list": [
                    {
                        "face": "http://i2.hdslb.com/bfs/face/d3d6f8659be3e309d6e58dd77accb3bb300215d5.jpg",
                        "id": 64494330,
                        "score": 10,
                        "uname": "我今天超可爱0"
                    }
                ],
                "level_info": {
                    "first_rank_img": "https://i0.hdslb.com/bfs/live/078e242c4e2bb380554d55d0ac479410d75a0efc.png",
                    "first_rank_name": "白银斗士",
                    "second_rank_icon": "https://i0.hdslb.com/bfs/live/1f8c2a959f92592407514a1afeb705ddc55429cd.png",
                    "second_rank_num": 1,
                    "uid": "688893202"
                },
                "pk_id": "100729259",
                "pk_type": "1",
                "result_info": {
                    "pk_extra_value": 0,
                    "pk_votes": 10,
                    "pk_votes_name": "乱斗值",
                    "total_score": 12
                },
                "result_type": 2,
                "season_id": 29
            },
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006
        }*/
    }
    else
    {
        return false;
    }

    triggerCmdEvent(cmd, LiveDanmaku(json));
    return true;
}

bool BaseWindow::handlePK2(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    if (cmd == "PK_BATTLE_PRE") // 开始前的等待状态
    {
        pkPre(json);
    }
    else if (cmd == "PK_BATTLE_SETTLE") // 解决了对手？
    {
        /*{
            "cmd": "PK_BATTLE_SETTLE",
            "pk_id": 100729259,
            "pk_status": 401,
            "settle_status": 1,
            "timestamp": 1605748006,
            "data": {
                "battle_type": 1,
                "result_type": 2
            },
            "roomid": "22532956"
        }*/
        // result_type: 2赢，-1输

        triggerCmdEvent(cmd, LiveDanmaku());
    }
    else if (cmd == "PK_BATTLE_PRE_NEW")
    {
        /*{
            "cmd": "PK_BATTLE_PRE_NEW",
            "data": {
                "battle_type": 1,
                "end_win_task": null,
                "face": "http://i0.hdslb.com/bfs/face/4c0e444dbabe86a3c4a3c47b72e2e63bd4a96684.jpg",
                "match_type": 1,
                "pk_votes_name":"\xE4\xB9\xB1\xE6\x96\x97\xE5\x80\xBC",
                "pre_timer": 10,
                "room_id": 4857111,
                "season_id": 31,
                "uid": 14833326,
                "uname":"\xE5\x8D\x83\xE9\xAD\x82\xE5\x8D\xB0"
            },
            "pk_id": 200271102,
            "pk_status": 101,
            "roomid": 22532956,
            "timestamp": 1611152119
        }*/
    }
    else if (cmd == "PK_LOTTERY_START") // 大乱斗胜利后的抽奖，触发未知，实测在某次大乱斗送天空之翼后有
    {
        /*{
            "cmd": "PK_LOTTERY_START",
            "data": {
                "asset_animation_pic": "https://i0.hdslb.com/bfs/vc/03be4c2912a4bd9f29eca3dac059c0e3e3fc69ce.gif",
                "asset_icon": "https://i0.hdslb.com/bfs/vc/44c367b09a8271afa22853785849e65797e085a1.png",
                "from_user": {
                    "face": "http://i2.hdslb.com/bfs/face/f25b706762e00a9adfe13e6147650891dd6f69a0.jpg",
                    "uid": 688893202,
                    "uname": "娇娇子er"
                },
                "id": 200105856,
                "max_time": 120,
                "pk_id": 200105856,
                "room_id": 22532956,
                "thank_text": "恭喜<%娇娇子er%>赢得大乱斗PK胜利",
                "time": 120,
                "time_wait": 0,
                "title": "恭喜主播大乱斗胜利",
                "weight": 0
            }
        }*/
    }
    else
    {
        return false;
    }

    triggerCmdEvent(cmd, LiveDanmaku());

    return true;
}

void BaseWindow::userComeEvent(LiveDanmaku &danmaku)
{
    qint64 uid = danmaku.getUid();

    // [%come_time% > %timestamp%-3600]*%ai_name%，你回来了~ // 一小时内
    // [%come_time%>0, %come_time%<%timestamp%-3600*24]*%ai_name%，你终于来喽！
    int userCome = danmakuCounts->value("come/" + snum(uid)).toInt();
    danmaku.setNumber(userCome);
    danmaku.setPrevTimestamp(danmakuCounts->value("comeTime/"+snum(uid), 0).toLongLong());

    appendNewLiveDanmaku(danmaku);

    userCome++;
    danmakuCounts->setValue("come/"+snum(uid), userCome);
    danmakuCounts->setValue("comeTime/"+snum(uid), danmaku.getTimeline().toSecsSinceEpoch());

    dailyCome++;
    if (dailySettings)
        dailySettings->setValue("come", dailyCome);
    if (danmaku.isOpposite())
    {
        // 加到自己这边来，免得下次误杀（即只提醒一次）
        // 不过不能这么做，否则不会显示“对面”两个字了
        // myAudience.insert(uid);
    }
    else if (cmAudience.contains(uid))
    {
        if (cmAudience.value(uid) > 0)
        {
            danmaku.setViewReturn(true);
            danmaku.setPrevTimestamp(cmAudience[uid]);
            cmAudience[uid] = 0; // 标记为串门回来
            localNotify(danmaku.getNickname() + " 去对面串门回来");
        }
    }

    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    if (!justStart && ui->autoSendWelcomeCheck->isChecked()) // 发送欢迎
    {
        userComeTimes[uid] = currentTime;
        sendWelcomeIfNotRobot(danmaku);
    }
    else // 不发送欢迎，只是查看
    {
        userComeTimes[uid] = currentTime; // 直接更新了
        if (judgeRobot == 2)
        {
            judgeRobotAndMark(danmaku);
        }
    }
}

void BaseWindow::refreshBlockList()
{
    if (browserData.isEmpty())
    {
        statusLabel->setText("请先设置用户数据");
        return ;
    }

    // 刷新被禁言的列表
    QString url = "https://api.live.bilibili.com/liveact/ajaxGetBlockList?roomid="+roomId+"&page=1";
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonArray list = json.value("data").toArray();
        userBlockIds.clear();
        foreach (QJsonValue val, list)
        {
            QJsonObject obj = val.toObject();
            qint64 id = static_cast<qint64>(obj.value("id").toDouble());
            qint64 uid = static_cast<qint64>(obj.value("uid").toDouble());
            QString uname = obj.value("uname").toString();
            userBlockIds.insert(uid, id);
//            qDebug() << "已屏蔽:" << id << uname << uid;
        }

    });
}

bool BaseWindow::isInFans(qint64 uid)
{
    foreach (auto fan, fansList)
    {
        if (fan.mid == uid)
            return true;
    }
    return false;
}

void BaseWindow::sendGift(int giftId, int giftNum)
{
    if (roomId.isEmpty() || browserCookie.isEmpty())
    {
        qWarning() << "房间为空，或未登录";
        return ;
    }

    if (localDebug)
    {
        localNotify("赠送礼物 -> " + snum(giftId) + " x " + snum(giftNum));
        return ;
    }


    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "uid=" + cookieUid;
    datas << "gift_id=" + snum(giftId);
    datas << "ruid=" + upUid;
    datas << "send_ruid=0";
    datas << "gift_num=" + snum(giftNum);
    datas << "coin_type=gold";
    datas << "bag_id=0";
    datas << "platform=pc";
    datas << "biz_code=live";
    datas << "biz_id=" + roomId;
    datas << "rnd=" + snum(QDateTime::currentSecsSinceEpoch());
    datas << "storm_beat_id=0";
    datas << "metadata=";
    datas << "price=0";
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";

    QByteArray ba(datas.join("&").toStdString().data());

    QString url("https://api.live.bilibili.com/gift/v2/Live/send");
    post(url, ba, [=](QJsonObject json){
        QString message = json.value("message").toString();
        statusLabel->setText("");
        if (message != "success")
        {
            statusLabel->setText(message);
            qCritical() << s8("warning: 发送失败：") << message << datas.join("&");
        }
    });
}

void BaseWindow::sendBagGift(int giftId, int giftNum, qint64 bagId)
{
    if (roomId.isEmpty() || browserCookie.isEmpty())
    {
        qWarning() << "房间为空，或未登录";
        return ;
    }

    if (localDebug)
    {
        localNotify("赠送包裹礼物 -> " + snum(giftId) + " x " + snum(giftNum));
        return ;
    }

    // 设置数据（JSON的ByteArray）
    QStringList datas;
    datas << "uid=" + cookieUid;
    datas << "gift_id=" + snum(giftId);
    datas << "ruid=" + upUid;
    datas << "send_ruid=0";
    datas << "gift_num=" + snum(giftNum);
    datas << "bag_id=" + snum(bagId);
    datas << "platform=pc";
    datas << "biz_code=live";
    datas << "biz_id=" + roomId;
    datas << "rnd=" + snum(QDateTime::currentSecsSinceEpoch());
    datas << "storm_beat_id=0";
    datas << "metadata=";
    datas << "price=0";
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";

    QByteArray ba(datas.join("&").toStdString().data());

    QString url("https://api.live.bilibili.com/gift/v2/live/bag_send");
    post(url, ba, [=](QJsonObject json){
        QString message = json.value("message").toString();
        statusLabel->setText("");
        if (message != "success")
        {
            statusLabel->setText(message);
            qCritical() << s8("warning: 发送失败：") << message << datas.join("&");
        }
    });
}

void BaseWindow::getRoomLiveVideoUrl(StringFunc func)
{
    if (roomId.isEmpty())
        return ;
    QString url = "http://api.live.bilibili.com/room/v1/Room/playUrl?cid=" + roomId
            + "&quality=4&qn=10000&platform=web&otype=json";
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取链接
        QJsonArray array = json.value("data").toObject().value("durl").toArray();
        /*"url": "http://d1--cn-gotcha04.bilivideo.com/live-bvc/521719/live_688893202_7694436.flv?cdn=cn-gotcha04\\u0026expires=1607505058\\u0026len=0\\u0026oi=1944862322\\u0026pt=\\u0026qn=10000\\u0026trid=40938d869aca4cb39041730f74ff9051\\u0026sigparams=cdn,expires,len,oi,pt,qn,trid\\u0026sign=ac701ba60c346bf3a173e56bcb14b7b9\\u0026ptype=0\\u0026src=9\\u0026sl=1\\u0026order=1",
          "length": 0,
          "order": 1,
          "stream_type": 0,
          "p2p_type": 0*/
        QString url = array.first().toObject().value("url").toString();
        if (func)
            func(url);
    });
}

/**
 * 触发进入直播间事件
 * 偶然看到的，未经测试
 */
void BaseWindow::roomEntryAction()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/roomEntryAction?room_id="
            + roomId + "&platform=pc&csrf_token=" + csrf_token + "&csrf=" + csrf_token + "&visit_id=";
    post(url, QByteArray(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }
    });
}

void BaseWindow::sendExpireGift()
{
    getBagList(24 * 3600 * 2); // 默认赠送两天内过期的
}

void BaseWindow::getBagList(qint64 sendExpire)
{
    if (roomId.isEmpty() || browserCookie.isEmpty())
    {
        qWarning() << "房间为空，或未登录";
        return ;
    }
    /*{
        "code": 0,
        "message": "0",
        "ttl": 1,
        "data": {
            "list": [
                {
                    "bag_id": 232151929,
                    "gift_id": 30607,
                    "gift_name": "小心心",
                    "gift_num": 12,
                    "gift_type": 5,
                    "expire_at": 1612972800,
                    "corner_mark": "4天",
                    "corner_color": "",
                    "count_map": [
                        {
                            "num": 1,
                            "text": ""
                        },
                        {
                            "num": 12,
                            "text": "全部"
                        }
                    ],
                    "bind_roomid": 0,
                    "bind_room_text": "",
                    "type": 1,
                    "card_image": "",
                    "card_gif": "",
                    "card_id": 0,
                    "card_record_id": 0,
                    "is_show_send": false
                },
                ......
    }*/

    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/gift/bag_list?t=1612663775421&room_id=" + roomId;
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonArray bagArray = json.value("data").toObject().value("list").toArray();

        if (true)
        {
            foreach (QJsonValue val, bagArray)
            {
                QJsonObject bag = val.toObject();
                // 赠送礼物
                QString giftName = bag.value("gift_name").toString();
                int giftId = bag.value("gift_id").toInt();
                int giftNum = bag.value("gift_num").toInt();
                qint64 bagId = qint64(bag.value("bag_id").toDouble());
                QString cornerMark = bag.value("corner_mark").toString();
                // qDebug() << "当前礼物：" << giftName << "×" << giftNum << cornerMark << giftId <<bagId;
            }
        }

        if (sendExpire) // 赠送过期礼物
        {
            QList<int> whiteList{1, 6, 30607}; // 辣条、亿圆、小心心

            qint64 timestamp = QDateTime::currentSecsSinceEpoch();
            qint64 expireTs = timestamp + sendExpire; // 超过这个时间的都送
            foreach (QJsonValue val, bagArray)
            {
                QJsonObject bag = val.toObject();
                qint64 expire = qint64(bag.value("expire_at").toDouble());
                int giftId = bag.value("gift_id").toInt();
                if (!whiteList.contains(giftId) || expire == 0 || expire > expireTs)
                    continue ;

                // 赠送礼物
                QString giftName = bag.value("gift_name").toString();
                int giftNum = bag.value("gift_num").toInt();
                qint64 bagId = qint64(bag.value("bag_id").toDouble());
                QString cornerMark = bag.value("corner_mark").toString();
                qDebug() << "赠送过期礼物：" << giftId << giftName << "×" << giftNum << cornerMark << (expire-timestamp)/3600 << "小时";
                sendBagGift(giftId, giftNum, bagId);
            }
        }
    });
}

void BaseWindow::updateExistGuards(int page)
{
    if (page == 0) // 表示是入口
    {
        if (updateGuarding)
            return ;

        page = 1;
        currentGuards.clear();
        guardInfos.clear();
        updateGuarding = true;

        // 参数是0的话，自动判断是否需要
        if (browserCookie.isEmpty())
            return ;
    }

    const int pageSize = 29;

    auto judgeGuard = [=](QJsonObject user){
        /*{
            "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
            "guard_level": 3,
            "guard_sub_level": 0,
            "is_alive": 1,
            "medal_info": {
                "medal_color_border": 6809855,
                "medal_color_end": 5414290,
                "medal_color_start": 1725515,
                "medal_level": 23,
                "medal_name": "翊中人"
            },
            "rank": 71,
            "ruid": 5988102,
            "uid": 20285041,
            "username": "懒一夕智能科技"
        }*/
        QString username = user.value("username").toString();
        qint64 uid = static_cast<qint64>(user.value("uid").toDouble());
        int guardLevel = user.value("guard_level").toInt();
        guardInfos.append(LiveDanmaku(guardLevel, username, uid, QDateTime::currentDateTime()));
        currentGuards[uid] = username;

        int count = danmakuCounts->value("guard/" + snum(uid), 0).toInt();
        if (!count)
        {
            int count = 1;
            if (guardLevel == 3)
                count = 1;
            else if (guardLevel == 2)
                count = 10;
            else if (guardLevel == 1)
                count = 100;
            else
                qWarning() << "错误舰长等级：" << username << uid << guardLevel;
            danmakuCounts->setValue("guard/" + snum(uid), count);
            qDebug() << "设置舰长：" << username << uid << count;
        }
    };

    QString _upUid = upUid;
    QString url = "https://api.live.bilibili.com/xlive/app-room/v2/guardTab/topList?roomid="
            +roomId+"&page="+snum(page)+"&ruid="+upUid+"&page_size="+snum(pageSize);
    get(url, [=](QJsonObject json){
        if (_upUid != upUid)
        {
            updateGuarding = false;
            return ;
        }

        QJsonObject data = json.value("data").toObject();

        if (page == 1)
        {
            QJsonArray top3 = data.value("top3").toArray();
            foreach (QJsonValue val, top3)
            {
                judgeGuard(val.toObject());
            }
        }

        QJsonArray list = data.value("list").toArray();
        foreach (QJsonValue val, list)
        {
            judgeGuard(val.toObject());
        }

        // 下一页
        QJsonObject info = data.value("info").toObject();
        int num = info.value("num").toInt();
        if (page * pageSize + 3 < num)
            updateExistGuards(page + 1);
        else // 全部结束了
        {
            if (ui->saveMonthGuardCheck->isChecked())
                saveMonthGuard();

            if (dailySettings)
                dailySettings->setValue("guard_count", currentGuards.size());
            updateGuarding = false;
        }
    });
}

/**
 * 获取高能榜上的用户（仅取第一页就行了）
 */
void BaseWindow::updateOnlineGoldRank()
{
    /*{
        "code": 0,
        "message": "0",
        "ttl": 1,
        "data": {
            "onlineNum": 12,
            "OnlineRankItem": [
                {
                    "userRank": 1,
                    "uid": 8160635,
                    "name": "嘻嘻家の第二帅",
                    "face": "http://i2.hdslb.com/bfs/face/494fcc986807a944b79a027559d964c8b6b3addb.jpg",
                    "score": 3300,
                    "medalInfo": null,
                    "guard_level": 2
                },
                {
                    "userRank": 2,
                    "uid": 1274248,
                    "name": "贪睡的熊猫",
                    "face": "http://i1.hdslb.com/bfs/face/6241c9080e98a8988a3acc2df146236bad897be3.gif",
                    "score": 1782,
                    "medalInfo": {
                        "guardLevel": 3,
                        "medalColorStart": 1725515,
                        "medalColorEnd": 5414290,
                        "medalColorBorder": 6809855,
                        "medalName": "戒不掉",
                        "level": 21,
                        "targetId": 300702024,
                        "isLight": 1
                    },
                    "guard_level": 3
                },
                ...剩下10个...
            ],
            "ownInfo": {
                "uid": 20285041,
                "name": "懒一夕智能科技",
                "face": "http://i1.hdslb.com/bfs/face/29183e0e21b60c01a95bb5c281566edb22af0f43.jpg",
                "rank": -1,
                "needScore": 1,
                "score": 0,
                "guard_level": 0
            }
        }
    }*/
    QString _upUid = upUid;
    QString url = "https://api.live.bilibili.com/xlive/general-interface/v1/rank/getOnlineGoldRank?roomId="
            +pkRoomId+"&page="+snum(1)+"&ruid="+upUid+"&pageSize="+snum(50);
    onlineGoldRank.clear();

    get(url, [=](QJsonObject json){
        if (_upUid != upUid)
            return ;

        QStringList names;
        QJsonObject data = json.value("data").toObject();
        QJsonArray array = data.value("OnlineRankItem").toArray();
        foreach (auto val, array)
        {
            QJsonObject item = val.toObject();
            qint64 uid = qint64(item.value("uid").toDouble());
            QString name = item.value("name").toString();
            int guard_level = item.value("guard_level").toInt(); // 没戴牌子也会算进去
            int score = item.value("score").toInt(); // 金瓜子数量
            int rank = item.value("userRank").toInt(); // 1,2,3...

            names.append(name + " " + snum(guard_level));
            LiveDanmaku danmaku(name, uid, QDateTime(), false,
                                "", "", "");
            danmaku.setFirst(rank);
            danmaku.setTotalCoin(score);

            if (guard_level)
                danmaku.setGuardLevel(guard_level);

            if (!item.value("medalInfo").isNull())
            {
                QJsonObject medalInfo = item.value("medalInfo").toObject();

                QString anchorId = snum(qint64(medalInfo.value("targetId").toDouble()));
                if (medalInfo.contains("guardLevel") && anchorId == roomId)
                    danmaku.setGuardLevel(medalInfo.value("guardLevel").toInt());

                qint64 medalColor = qint64(medalInfo.value("medalColorStart").toDouble());
                QString cs = QString::number(medalColor, 16);
                while (cs.size() < 6)
                    cs = "0" + cs;

                danmaku.setMedal(anchorId,
                                 medalInfo.value("medalName").toString(),
                                 medalInfo.value("level").toInt(),
                                 "",
                                 "");
            }

            onlineGoldRank.append(danmaku);
        }
        qDebug() << "高能榜：" << names;
    });
}

/**
 * 添加本次直播的金瓜子礼物
 */
void BaseWindow::appendLiveGift(const LiveDanmaku &danmaku)
{
    if (danmaku.getTotalCoin() == 0)
    {
        qWarning() << "添加礼物到liveGift错误：" << danmaku.toString();
        return ;
    }
    for (int i = 0; i < liveAllGifts.size(); i++)
    {
        auto his = liveAllGifts.at(i);
        if (his.getUid() == danmaku.getUid()
                && his.getGiftId() == danmaku.getGiftId())
        {
            liveAllGifts[i].addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());
            return ;
        }
    }

    // 新建一个
    liveAllGifts.append(danmaku);
}

void BaseWindow::appendLiveGuard(const LiveDanmaku &danmaku)
{
    if (!danmaku.is(MSG_GUARD_BUY))
    {
        qWarning() << "添加上船到liveGuard错误：" << danmaku.toString();
        return ;
    }
    for (int i = 0; i < liveAllGuards.size(); i++)
    {
        auto his = liveAllGuards.at(i);
        if (his.getUid() == danmaku.getUid()
                && his.getGiftId() == danmaku.getGiftId())
        {
            liveAllGuards[i].addGift(danmaku.getNumber(), danmaku.getTotalCoin(), danmaku.getTimeline());
            return ;
        }
    }

    // 新建一个
    liveAllGuards.append(danmaku);
}

void BaseWindow::getPkMatchInfo()
{
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getInfoByRoom?room_id=" + pkRoomId;
    get(url, [=](QJsonObject json) {
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        triggerCmdEvent("PK_MATCH_INFO", LiveDanmaku(data));
    });
}

void BaseWindow::getPkOnlineGuardPage(int page)
{
    static int guard1 = 0, guard2 = 0, guard3 = 0;
    if (page == 0)
    {
        page = 1;
        guard1 = guard2 = guard3 = 0;
    }

    auto addCount = [=](MyJson user) {
        int alive = user.i("is_alive");
        if (!alive)
            return ;
        QString username = user.s("username");
        int guard_level = user.i("guard_level");
        if (!alive)
            return ;
        if (guard_level == 1)
            guard1++;
        else if (guard_level == 2)
            guard2++;
        else
            guard3++;
    };

    QString url = "https://api.live.bilibili.com/xlive/app-room/v2/guardTab/topList?actionKey=appkey&appkey=27eb53fc9058f8c3&roomid=" + pkRoomId
            +"&page=" + QString::number(page) + "&ruid=" + pkUid + "&page_size=30";
    get(url, [=](MyJson json){
        MyJson data = json.data();
        // top3
        if (page == 1)
        {
            QJsonArray top3 = data.value("top3").toArray();
            foreach (QJsonValue val, top3)
                addCount(val.toObject());
        }

        // list
        QJsonArray list = data.value("list").toArray();
        foreach (QJsonValue val, list)
            addCount(val.toObject());

        // 下一页
        QJsonObject info = data.value("info").toObject();
        int page = info.value("page").toInt();
        int now = info.value("now").toInt();
        if (now < page)
            getPkOnlineGuardPage(now+1);
        else // 全部完成了
        {
            qDebug() << guard1 << guard2 << guard3;
            LiveDanmaku danmaku;
            danmaku.setNumber(guard1 + guard2 + guard3);
            danmaku.extraJson.insert("guard1", guard1);
            danmaku.extraJson.insert("guard2", guard2);
            danmaku.extraJson.insert("guard3", guard3);
            triggerCmdEvent("PK_MATCH_ONLINE_GUARD", danmaku);
        }
    });
}

void BaseWindow::on_autoSendWelcomeCheck_stateChanged(int arg1)
{
    settings->setValue("danmaku/sendWelcome", ui->autoSendWelcomeCheck->isChecked());
    ui->sendWelcomeTextCheck->setEnabled(arg1);
    ui->sendWelcomeVoiceCheck->setEnabled(arg1);
}

void BaseWindow::on_autoSendGiftCheck_stateChanged(int arg1)
{
    settings->setValue("danmaku/sendGift", ui->autoSendGiftCheck->isChecked());
    ui->sendGiftTextCheck->setEnabled(arg1);
    ui->sendGiftVoiceCheck->setEnabled(arg1);
}

void BaseWindow::on_autoWelcomeWordsEdit_textChanged()
{
    settings->setValue("danmaku/autoWelcomeWords", ui->autoWelcomeWordsEdit->toPlainText());
}

void BaseWindow::on_autoThankWordsEdit_textChanged()
{
    settings->setValue("danmaku/autoThankWords", ui->autoThankWordsEdit->toPlainText());
}

void BaseWindow::on_startLiveWordsEdit_editingFinished()
{
    settings->setValue("live/startWords", ui->startLiveWordsEdit->text());
}

void BaseWindow::on_endLiveWordsEdit_editingFinished()
{
    settings->setValue("live/endWords", ui->endLiveWordsEdit->text());
}

void BaseWindow::on_startLiveSendCheck_stateChanged(int arg1)
{
    settings->setValue("live/startSend", ui->startLiveSendCheck->isChecked());
}

void BaseWindow::on_autoSendAttentionCheck_stateChanged(int arg1)
{
    settings->setValue("danmaku/sendAttention", ui->autoSendAttentionCheck->isChecked());
    ui->sendAttentionTextCheck->setEnabled(arg1);
    ui->sendAttentionVoiceCheck->setEnabled(arg1);
}

void BaseWindow::on_autoAttentionWordsEdit_textChanged()
{
    settings->setValue("danmaku/autoAttentionWords", ui->autoAttentionWordsEdit->toPlainText());
}

void BaseWindow::on_sendWelcomeCDSpin_valueChanged(int arg1)
{
    settings->setValue("danmaku/sendWelcomeCD", arg1);
}

void BaseWindow::on_sendGiftCDSpin_valueChanged(int arg1)
{
    settings->setValue("danmaku/sendGiftCD", arg1);
}

void BaseWindow::on_sendAttentionCDSpin_valueChanged(int arg1)
{
    settings->setValue("danmaku/sendAttentionCD", arg1);
}

void BaseWindow::showDiangeHistory()
{
    QStringList list;
    int first = qMax(0, diangeHistory.size() - 10);
    for (int i = diangeHistory.size()-1; i >= first; i--)
    {
        Diange dg = diangeHistory.at(i);
        list << dg.name + "  -  " + dg.nickname + "  " + dg.time.toString("hh:mm:ss");
    }
    QString text = list.size() ? list.join("\n") : "没有点歌记录";
    QMessageBox::information(this, "点歌历史", text);
}

void BaseWindow::addBlockUser(qint64 uid, int hour)
{
    addBlockUser(uid, roomId.toLongLong(), hour);
}

void BaseWindow::addBlockUser(qint64 uid, qint64 roomId, int hour)
{
    if(browserData.isEmpty())
    {
        statusLabel->setText("请先设置登录信息");
        return ;
    }

    if (localDebug)
    {
        localNotify("禁言用户 -> " + snum(uid) + " " + snum(hour) + " 小时");
        return ;
    }

    QString url = "https://api.live.bilibili.com/banned_service/v2/Silent/add_block_user";
    QString data = QString("roomid=%1&block_uid=%2&hour=%3&csrf_token=%4&csrd=%5&visit_id=")
                    .arg(roomId).arg(uid).arg(hour).arg(csrf_token).arg(csrf_token);
    qInfo() << "禁言：" << uid << hour;
    post(url, data.toStdString().data(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            statusLabel->setText(json.value("message").toString());
            qWarning() << "禁言失败：" << json.value("message").toString();
            return ;
        }
        QJsonObject d = json.value("data").toObject();
        qint64 id = static_cast<qint64>(d.value("id").toDouble());
        userBlockIds[uid] = id;
    });
}

void BaseWindow::delBlockUser(qint64 uid)
{
    delBlockUser(uid, roomId.toLongLong());
}

void BaseWindow::delBlockUser(qint64 uid, qint64 roomId)
{
    if(browserData.isEmpty())
    {
        statusLabel->setText("请先设置登录信息");
        return ;
    }

    if (localDebug)
    {
        localNotify("取消禁言 -> " + snum(uid));
        return ;
    }

    if (userBlockIds.contains(uid))
    {
        qDebug() << "取消用户：" << uid << "  id =" << userBlockIds.value(uid);
        delRoomBlockUser(userBlockIds.value(uid));
        userBlockIds.remove(uid);
        return ;
    }

    // 获取直播间的网络ID，再取消屏蔽
    QString url = "https://api.live.bilibili.com/liveact/ajaxGetBlockList?roomid="+snum(roomId)+"&page=1";
    get(url, [=](QJsonObject json){
        int code = json.value("code").toInt();
        if (code != 0)
        {
            statusLabel->setText(json.value("message").toString());
            if(statusLabel->text().isEmpty() && code == 403)
                statusLabel->setText("您没有权限");
            return ;
        }
        QJsonArray list = json.value("data").toArray();
        foreach (QJsonValue val, list)
        {
            QJsonObject obj = val.toObject();
            if (static_cast<qint64>(obj.value("uid").toDouble()) == uid)
            {
                delRoomBlockUser(static_cast<qint64>(obj.value("id").toDouble())); // 获取房间ID
                break;
            }
        }

    });
}

void BaseWindow::delRoomBlockUser(qint64 id)
{
    QString url = "https://api.live.bilibili.com/banned_service/v1/Silent/del_room_block_user";
    QString data = QString("id=%1&roomid=%2&csrf_token=%4&csrd=%5&visit_id=")
                    .arg(id).arg(roomId).arg(csrf_token).arg(csrf_token);

    post(url, data.toStdString().data(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            statusLabel->setText(json.value("message").toString());
            return ;
        }

        // if (userBlockIds.values().contains(id))
        //    userBlockIds.remove(userBlockIds.key(id));
    });
}

void BaseWindow::eternalBlockUser(qint64 uid, QString uname)
{
    if (eternalBlockUsers.contains(EternalBlockUser(uid, roomId.toLongLong())))
    {
        localNotify("该用户已经在永久禁言中");
        return ;
    }

    addBlockUser(uid, 720);

    eternalBlockUsers.append(EternalBlockUser(uid, roomId.toLongLong(), uname, upName, roomTitle, QDateTime::currentSecsSinceEpoch()));
    saveEternalBlockUsers();
    qDebug() << "添加永久禁言：" << uname << "    当前人数：" << eternalBlockUsers.size();
}

void BaseWindow::cancelEternalBlockUser(qint64 uid)
{
    EternalBlockUser user(uid, roomId.toLongLong());
    if (!eternalBlockUsers.contains(user))
        return ;

    eternalBlockUsers.removeOne(user);
    saveEternalBlockUsers();
    qDebug() << "移除永久禁言：" << uid << "    当前人数：" << eternalBlockUsers.size();
}

void BaseWindow::cancelEternalBlockUserAndUnblock(qint64 uid)
{
    cancelEternalBlockUser(uid);

    delBlockUser(uid);
}

void BaseWindow::saveEternalBlockUsers()
{
    QJsonArray array;
    int size = eternalBlockUsers.size();
    for (int i = 0; i < size; i++)
        array.append(eternalBlockUsers.at(i).toJson());
    settings->setValue("danmaku/eternalBlockUsers", array);
    qDebug() << "保存永久禁言，当前人数：" << eternalBlockUsers.size();
}

/**
 * 检测需要重新禁言的用户，越前面时间越早
 */
void BaseWindow::detectEternalBlockUsers()
{
    qint64 currentSecond = QDateTime::currentSecsSinceEpoch();
    const int MAX_BLOCK_HOUR = 720;
    qint64 maxBlockSecond = MAX_BLOCK_HOUR * 3600;
    const int netDelay = 5; // 5秒的屏蔽时长
    bool blocked = false;
    for (int i = 0; i < eternalBlockUsers.size(); i++)
    {
        EternalBlockUser user = eternalBlockUsers.first();
        if (user.time + maxBlockSecond + netDelay >= currentSecond) // 仍在冷却中
            break;

        // 该补上禁言啦
        blocked = true;
        qDebug() << "永久禁言：重新禁言用户" << user.uid << user.uname << user.time << "->" << currentSecond;
        addBlockUser(user.uid, user.roomId, MAX_BLOCK_HOUR);

        user.time = currentSecond;
        eternalBlockUsers.removeFirst();
        eternalBlockUsers.append(user);
    }
    if (blocked)
        saveEternalBlockUsers();
}

void BaseWindow::on_enableBlockCheck_clicked()
{
    bool enable = ui->enableBlockCheck->isChecked();
    settings->setValue("block/enableBlock", enable);
    if (danmakuWindow)
        danmakuWindow->setEnableBlock(enable);

    if (enable)
        refreshBlockList();
}


void BaseWindow::on_newbieTipCheck_clicked()
{
    bool enable = ui->newbieTipCheck->isChecked();
    settings->setValue("block/newbieTip", enable);
    if (danmakuWindow)
        danmakuWindow->setNewbieTip(enable);
}

void BaseWindow::on_autoBlockNewbieCheck_clicked()
{
    settings->setValue("block/autoBlockNewbie", ui->autoBlockNewbieCheck->isChecked());
    ui->autoBlockNewbieNotifyCheck->setEnabled(ui->autoBlockNewbieCheck->isChecked());
}

void BaseWindow::on_autoBlockNewbieKeysEdit_textChanged()
{
    settings->setValue("block/autoBlockNewbieKeys", ui->autoBlockNewbieKeysEdit->toPlainText());
}

void BaseWindow::on_autoBlockNewbieNotifyCheck_clicked()
{
    settings->setValue("block/autoBlockNewbieNotify", ui->autoBlockNewbieNotifyCheck->isChecked());
}

void BaseWindow::on_autoBlockNewbieNotifyWordsEdit_textChanged()
{
    settings->setValue("block/autoBlockNewbieNotifyWords", ui->autoBlockNewbieNotifyWordsEdit->toPlainText());
}

void BaseWindow::on_saveDanmakuToFileCheck_clicked()
{
    bool enabled = ui->saveDanmakuToFileCheck->isChecked();
    settings->setValue("danmaku/saveDanmakuToFile", enabled);
    if (enabled)
        startSaveDanmakuToFile();
    else
        finishSaveDanmuToFile();
}

void BaseWindow::on_promptBlockNewbieCheck_clicked()
{
    settings->setValue("block/promptBlockNewbie", ui->promptBlockNewbieCheck->isChecked());
}

void BaseWindow::on_promptBlockNewbieKeysEdit_textChanged()
{
    settings->setValue("block/promptBlockNewbieKeys", ui->promptBlockNewbieKeysEdit->toPlainText());
}

void BaseWindow::on_timerConnectServerCheck_clicked()
{
    bool enable = ui->timerConnectServerCheck->isChecked();
    settings->setValue("live/timerConnectServer", enable);
    if (!isLiving() && enable)
        startConnectRoom();
    else if (!enable && (!socket || socket->state() == QAbstractSocket::UnconnectedState))
        startConnectRoom();
}

void BaseWindow::on_startLiveHourSpin_valueChanged(int arg1)
{
    settings->setValue("live/startLiveHour", ui->startLiveHourSpin->value());
    if (!justStart && ui->timerConnectServerCheck->isChecked() && connectServerTimer && !connectServerTimer->isActive())
        connectServerTimer->start();
}

void BaseWindow::on_endLiveHourSpin_valueChanged(int arg1)
{
    settings->setValue("live/endLiveHour", ui->endLiveHourSpin->value());
    if (!justStart && ui->timerConnectServerCheck->isChecked() && connectServerTimer && !connectServerTimer->isActive())
        connectServerTimer->start();
}

void BaseWindow::on_calculateDailyDataCheck_clicked()
{
    bool enable = ui->calculateDailyDataCheck->isChecked();
    settings->setValue("live/calculateDaliyData", enable);
    if (enable)
        startCalculateDailyData();
}

void BaseWindow::on_pushButton_clicked()
{
    QString text = QDateTime::currentDateTime().toString("yyyy-MM-dd\n");
    text += "\n进来人次：" + snum(dailyCome);
    text += "\n观众人数：" + snum(userComeTimes.count());
    text += "\n弹幕数量：" + snum(dailyDanmaku);
    text += "\n新人弹幕：" + snum(dailyNewbieMsg);
    text += "\n新增关注：" + snum(dailyNewFans);
    text += "\n银瓜子数：" + snum(dailyGiftSilver);
    text += "\n金瓜子数：" + snum(dailyGiftGold);
    text += "\n上船次数：" + snum(dailyGuard);
    text += "\n最高人气：" + snum(dailyMaxPopul);
    text += "\n平均人气：" + snum(dailyAvePopul);

    text += "\n\n累计粉丝：" + snum(currentFans);
    QMessageBox::information(this, "今日数据", text);
}

void BaseWindow::on_removeDanmakuTipIntervalSpin_valueChanged(int arg1)
{
    this->removeDanmakuTipInterval = arg1 * 1000;
    settings->setValue("danmaku/removeTipInterval", arg1);
}

void BaseWindow::on_doveCheck_clicked()
{
    // 这里不做操作，重启失效
}

void BaseWindow::on_notOnlyNewbieCheck_clicked()
{
    bool enable = ui->notOnlyNewbieCheck->isChecked();
    settings->setValue("block/notOnlyNewbie", enable);
}

void BaseWindow::on_pkAutoMelonCheck_clicked()
{
    bool enable = ui->pkAutoMelonCheck->isChecked();
    settings->setValue("pk/autoMelon", enable);
}

void BaseWindow::on_pkMaxGoldButton_clicked()
{
    bool ok = false;
    // 最大设置的是1000元，有钱人……
    int v = QInputDialog::getInt(this, "自动偷塔礼物上限", "单次PK偷塔赠送的金瓜子上限\n1元=1000金瓜子=100或10乱斗值（按B站规则会变）\n注意：每次偷塔、反偷塔时都单独判断上限，而非一次大乱斗的累加", pkMaxGold, 0, 1000000, 100, &ok);
    if (!ok)
        return ;

    settings->setValue("pk/maxGold", pkMaxGold = v);
}

void BaseWindow::on_pkJudgeEarlyButton_clicked()
{
    bool ok = false;
    int v = QInputDialog::getInt(this, "自动偷塔提前判断", "每次偷塔结束前n毫秒判断，根据网速酌情设置\n1秒=1000毫秒，下次大乱斗生效", pkJudgeEarly, 0, 5000, 500, &ok);
    if (!ok)
        return ;

    settings->setValue("pk/judgeEarly", pkJudgeEarly = v);
}

template<typename T>
bool BaseWindow::isConditionTrue(T a, T b, QString op) const
{
    if (op == "==" || op == "=")
        return a == b;
    if (op == "!=" || op == "<>")
        return a != b;
    if (op == ">")
        return a > b;
    if (op == ">=")
        return a >= b;
    if (op == "<")
        return a < b;
    if (op == "<=")
        return a <= b;
    qWarning() << "无法识别的比较模板类型：" << a << op << b;
    return false;
}

void BaseWindow::on_roomIdEdit_returnPressed()
{
    if (socket->state() == QAbstractSocket::UnconnectedState)
    {
        startConnectRoom();
    }
}

void BaseWindow::on_actionData_Path_triggered()
{
    QDesktopServices::openUrl(QUrl("file:///" + dataPath, QUrl::TolerantMode));
}

/**
 * 显示实时弹幕
 */
void BaseWindow::on_actionShow_Live_Danmaku_triggered()
{
    if (!danmakuWindow)
    {
        danmakuWindow = new LiveDanmakuWindow(settings, dataPath, this);

        connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), danmakuWindow, SLOT(slotNewLiveDanmaku(LiveDanmaku)));
        connect(this, SIGNAL(signalRemoveDanmaku(LiveDanmaku)), danmakuWindow, SLOT(slotOldLiveDanmakuRemoved(LiveDanmaku)));
        connect(danmakuWindow, SIGNAL(signalSendMsg(QString)), this, SLOT(sendMsg(QString)));
        connect(danmakuWindow, SIGNAL(signalAddBlockUser(qint64, int)), this, SLOT(addBlockUser(qint64, int)));
        connect(danmakuWindow, SIGNAL(signalDelBlockUser(qint64)), this, SLOT(delBlockUser(qint64)));
        connect(danmakuWindow, SIGNAL(signalEternalBlockUser(qint64,QString)), this, SLOT(eternalBlockUser(qint64,QString)));
        connect(danmakuWindow, SIGNAL(signalCancelEternalBlockUser(qint64)), this, SLOT(cancelEternalBlockUser(qint64)));
        connect(danmakuWindow, SIGNAL(signalAIReplyed(QString, qint64)), this, SLOT(slotAIReplyed(QString, qint64)));
        connect(danmakuWindow, SIGNAL(signalShowPkVideo()), this, SLOT(on_actionShow_PK_Video_triggered()));
        connect(danmakuWindow, &LiveDanmakuWindow::signalChangeWindowMode, this, [=]{
            danmakuWindow->deleteLater();
            danmakuWindow = nullptr;
            on_actionShow_Live_Danmaku_triggered(); // 重新加载
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalSendMsgToPk, this, [=](QString msg){
            if (!pking || pkRoomId.isEmpty())
                return ;
            qDebug() << "发送PK对面消息：" << pkRoomId << msg;
            sendRoomMsg(pkRoomId, msg);
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalMarkUser, this, [=](qint64 uid){
            if (judgeRobot)
                markNotRobot(uid);
        });
        connect(danmakuWindow, &LiveDanmakuWindow::signalTransMouse, this, [=](bool enabled){
            if (enabled)
                ui->closeTransMouseButton->show();
            else
                ui->closeTransMouseButton->hide();
        });
        danmakuWindow->setEnableBlock(ui->enableBlockCheck->isChecked());
        danmakuWindow->setNewbieTip(ui->newbieTipCheck->isChecked());
        danmakuWindow->setIds(upUid.toLongLong(), roomId.toLongLong());
        danmakuWindow->setWindowIcon(this->windowIcon());
        danmakuWindow->setWindowTitle(this->windowTitle());
        danmakuWindow->hide();

        QTimer::singleShot(0, [=]{
            danmakuWindow->removeAll();
            if (roomDanmakus.size())
            {
                for (int i = 0; i < roomDanmakus.size(); i++)
                    danmakuWindow->slotNewLiveDanmaku(roomDanmakus.at(i));
                danmakuWindow->setAutoTranslate(ui->languageAutoTranslateCheck->isChecked());
                danmakuWindow->setAIReply(ui->AIReplyCheck->isChecked());

                if (pking)
                {
                    danmakuWindow->setIds(upUid.toLongLong(), roomId.toLongLong());
                }
            }
            else // 没有之前的弹幕，从API重新pull下来
            {
                pullLiveDanmaku();
            }
        });
    }

    bool hidding = danmakuWindow->isHidden();

    if (hidding)
    {
        danmakuWindow->show();
    }
    else
    {
        danmakuWindow->hide();
    }
    settings->setValue("danmaku/liveWindow", hidding);
}

void BaseWindow::on_actionSet_Cookie_triggered()
{
    bool ok = false;
    QString s = QInputDialog::getText(this, "设置Cookie", "设置用户登录的cookie", QLineEdit::Normal, browserCookie, &ok);
    if (!ok)
        return ;

    if (!s.contains("SESSDATA="))
    {
        QMessageBox::warning(this, "设置Cookie", "设置Cookie失败，这不是Bilibili的浏览器Cookie");
        return ;
    }

    if (s.toLower().startsWith("cookie:"))
        s = s.replace(0, 7, "").trimmed();

    autoSetCookie(s);
}

void BaseWindow::on_actionSet_Danmaku_Data_Format_triggered()
{
    bool ok = false;
    QString s = QInputDialog::getText(this, "设置Data", "设置弹幕的data\n自动从cookie中提取，可不用设置", QLineEdit::Normal, browserData, &ok);
    if (!ok)
        return ;

    settings->setValue("danmaku/browserData", browserData = s);
    int posl = browserData.indexOf("csrf_token=") + 9;
    int posr = browserData.indexOf("&", posl);
    if (posr == -1) posr = browserData.length();
    csrf_token = browserData.mid(posl, posr - posl);
}

void BaseWindow::on_actionCookie_Help_triggered()
{
    QString steps = "发送弹幕前需按以下步骤注入登录信息：\n\n";
    steps += "步骤一：\n浏览器登录bilibili账号，按下F12（开发者调试工具）\n\n";
    steps += "步骤二：\n找到右边顶部的“Network”项，选择它下面的XHR\n\n";
    steps += "步骤三：\n刷新B站页面，中间多出一排列表，点其中任意一个，看右边“Headers”中的代码\n\n";
    steps += "步骤四：\n复制“Request Headers”下的“cookie”冒号后的一长串内容，粘贴到本程序“设置Cookie”中\n\n";
    steps += "设置好直播间ID、要发送的内容，即可发送弹幕！\n";
    steps += "注意：请勿过于频繁发送，容易被临时拉黑！";

    /*steps += "\n\n变量列表：\n";
    steps += "\\n：分成多条弹幕发送，间隔1.5秒";
    steps += "\n%hour%：根据时间替换为“早上”、“中午”、“晚上”等";
    steps += "\n%all_greet%：根据时间替换为“你好啊”、“早上好呀”、“晚饭吃了吗”、“还没睡呀”等";
    steps += "\n%greet%：根据时间替换为“你好”、“早上好”、“中午好”等";
    steps += "\n%tone%：随机替换为“啊”、“呀”";
    steps += "\n%punc%：随机替换为“~”、“！”";
    steps += "\n%tone/punc%：随机替换为“啊”、“呀”、“~”、“！”";*/
    QMessageBox::information(this, "定时弹幕", steps);
}

void BaseWindow::on_actionCreate_Video_LRC_triggered()
{
    VideoLyricsCreator* vlc = new VideoLyricsCreator(settings, nullptr);
    vlc->show();
}

void BaseWindow::on_actionShow_Order_Player_Window_triggered()
{
    if (!musicWindow)
    {
        musicWindow = new OrderPlayerWindow(dataPath, nullptr);
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongSucceed, this, [=](Song song, qint64 latency, int waiting){
            qDebug() << "点歌成功" << song.simpleString() << latency;
            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setPrevTimestamp(latency / 1000); // 毫秒转秒
            danmaku.setFirst(waiting);
            triggerCmdEvent("ORDER_SONG_SUCCEED", danmaku);
            triggerCmdEvent("ORDER_SONG_SUCCEED_OVERRIDE", danmaku);
            if (hasEvent("ORDER_SONG_SUCCEED_OVERRIDE"))
                return ;
            if (latency < 180000) // 小于3分钟
            {
                QString tip = "成功点歌：【" + song.simpleString() + "】";
                localNotify(tip);
                if (ui->diangeReplyCheck->isChecked())
                {
                    if (waiting == 1 && latency > 20000)
                        sendNotifyMsg("成功点歌，下一首播放");
                    else if (waiting == 2 && latency > 20000)
                        sendNotifyMsg("成功点歌，下两首播放");
                    else // 多首队列
                        sendNotifyMsg("成功点歌");
                }
            }
            else // 超过3分钟
            {
                int minute = (latency+20000) / 60000;
                localNotify(snum(minute) + "分钟后播放【" + song.simpleString() + "】");
                if (ui->diangeReplyCheck->isChecked())
                    sendNotifyMsg("成功点歌，" + snum(minute) + "分钟后播放");
            }
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongPlayed, this, [=](Song song){
            localNotify("开始播放：" + song.simpleString());
            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setPrevTimestamp(song.addTime);
            triggerCmdEvent("ORDER_SONG_PLAY", danmaku);
        });
        connect(musicWindow, &OrderPlayerWindow::signalCurrentSongChanged, this, [=](Song song){
            if (sendCurrentSongToSockets)
                sendJsonToSockets("CURRENT_SONG", song.toJson());

            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setPrevTimestamp(song.addTime);
            triggerCmdEvent("CURRENT_SONG_CHANGED", danmaku);
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongImproved, this, [=](Song song, int prev, int curr){
            localNotify("提升歌曲：" + song.name + " : " + snum(prev+1) + "->" + snum(curr+1));
            LiveDanmaku danmaku(song.id, song.addBy, song.name);
            danmaku.setNumber(curr+1);
            triggerCmdEvent("ORDER_SONG_IMPROVED", danmaku);
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongCutted, this, [=](Song song){
            localNotify("已切歌");
            triggerCmdEvent("ORDER_SONG_CUTTED", LiveDanmaku(song.id, song.addBy, song.name));
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongModified, this, [=](const SongList& songs){
            if (ui->orderSongsToFileCheck->isChecked())
            {
                saveOrderSongs(songs);
            }
            if (sendSongListToSockets)
            {
                sendMusicList(songs);
            }
        });
        connect(musicWindow, &OrderPlayerWindow::signalLyricChanged, this, [=](){
            if (ui->songLyricsToFileCheck->isChecked())
            {
                saveSongLyrics();
            }
            if (sendLyricListToSockets)
            {
                sendLyricList();
            }
        });
        auto simulateMusicKey = [=]{
            if (!ui->autoPauseOuterMusicCheck->isChecked())
                return ;
#if defined (Q_OS_WIN)
            simulateKeys(ui->outerMusicKeyEdit->text());
#endif
        };
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongStarted, this, [=]{
            simulateMusicKey();
        });
        connect(musicWindow, &OrderPlayerWindow::signalOrderSongEnded, this, [=]{
            simulateMusicKey();
        });
        connect(musicWindow, &OrderPlayerWindow::signalWindowClosed, this, [=]{
            QTimer::singleShot(5000, this, [=]{ // 延迟5秒，避免程序关闭时先把点歌姬关了，但下次还是需要显示的
                settings->setValue("danmaku/playerWindow", false);
            });
        });
    }

    bool hidding = musicWindow->isHidden();

    if (hidding)
    {
        musicWindow->show();
    }
    else
    {
        musicWindow->hide();
    }
    settings->setValue("danmaku/playerWindow", hidding);
}

void BaseWindow::on_diangeReplyCheck_clicked()
{
    settings->setValue("danmaku/diangeReply", ui->diangeReplyCheck->isChecked());
}

void BaseWindow::on_actionAbout_triggered()
{
    QString appVersion = GetFileVertion(QApplication::applicationFilePath()).trimmed();
    if (!appVersion.startsWith("v") && !appVersion.startsWith("V"))
        appVersion.insert(0, "v");

    QString text;
    text += QApplication::applicationName() + " " + appVersion;
    text += "\n\n本程序由心乂独立开发，参考多个开源项目实现。\n仅供个人学习、研究之用，禁止用于商业用途。\n\n";
    text += "QQ群：1038738410\n欢迎大家一起来交流反馈&功能研发&闲聊&搞事情&拯救地球\n\n";
    text += "GitHub: https://github.com/iwxyi";
    QMessageBox::information(this, "关于", text);
}

void BaseWindow::on_actionGitHub_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/iwxyi/Bilibili-MagicalDanmaku"));
}

void BaseWindow::on_actionCustom_Variant_triggered()
{
    QString text = saveCustomVariant();
    bool ok;
    text = TextInputDialog::getText(this, "自定义变量", "请输入自定义变量，可在答谢、定时中使用：\n示例格式：%var%=val", text, &ok);
    if (!ok)
        return ;
    settings->setValue("danmaku/customVariant", text);

    restoreCustomVariant(text);
}

void BaseWindow::on_actionVariant_Translation_triggered()
{
}

void BaseWindow::on_actionSend_Long_Text_triggered()
{
    bool ok;
    QString text = QInputDialog::getText(this, "发送长文本", "请输入长文本（支持变量），分割为每次20字发送\n注意带有敏感词或特殊字符的部分将无法发送", QLineEdit::Normal, "", &ok);
    text = text.trimmed();
    if (!ok || text.isEmpty())
        return ;

    sendLongText(text);
}

void BaseWindow::on_actionShow_Lucky_Draw_triggered()
{
    if (!luckyDrawWindow)
    {
        luckyDrawWindow = new LuckyDrawWindow(nullptr);
        connect(this, SIGNAL(signalNewDanmaku(LiveDanmaku)), luckyDrawWindow, SLOT(slotNewDanmaku(LiveDanmaku)));
    }
    luckyDrawWindow->show();
}

void BaseWindow::on_actionGet_Play_Url_triggered()
{
    getRoomLiveVideoUrl([=](QString url) {
        QApplication::clipboard()->setText(url);
    });
}

void BaseWindow::on_actionShow_Live_Video_triggered()
{
    if (roomId.isEmpty())
        return ;

    LiveVideoPlayer* player = new LiveVideoPlayer(settings, nullptr);
    connect(this, SIGNAL(signalLiveStart(QString)), player, SLOT(slotLiveStart(QString))); // 重新开播，需要刷新URL
    connect(player, SIGNAL(signalRestart()), this, SLOT(on_actionShow_Live_Video_triggered()));
    player->setAttribute(Qt::WA_DeleteOnClose, true);
    player->setRoomId(roomId);
    player->setWindowTitle(roomTitle + " - " + upName);
    player->setWindowIcon(upFace);
    player->show();
}

void BaseWindow::on_actionShow_PK_Video_triggered()
{
    if (pkRoomId.isEmpty())
        return ;

    LiveVideoPlayer* player = new LiveVideoPlayer(settings, nullptr);
    player->setAttribute(Qt::WA_DeleteOnClose, true);
    player->setRoomId(pkRoomId);
    player->show();
}

void BaseWindow::on_pkChuanmenCheck_clicked()
{
    pkChuanmenEnable = ui->pkChuanmenCheck->isChecked();
    settings->setValue("pk/chuanmen", pkChuanmenEnable);

    if (pkChuanmenEnable)
    {
        connectPkRoom();
    }
}

void BaseWindow::on_pkMsgSyncCheck_clicked()
{
    if (pkMsgSync == 0)
    {
        pkMsgSync = 1;
        ui->pkMsgSyncCheck->setCheckState(Qt::PartiallyChecked);
    }
    else if (pkMsgSync == 1)
    {
        pkMsgSync = 2;
        ui->pkMsgSyncCheck->setCheckState(Qt::Checked);
    }
    else if (pkMsgSync == 2)
    {
        pkMsgSync = 0;
        ui->pkMsgSyncCheck->setCheckState(Qt::Unchecked);
    }
    ui->pkMsgSyncCheck->setText(pkMsgSync == 1 ? "PK同步消息(仅视频)" : "PK同步消息");
    settings->setValue("pk/msgSync", pkMsgSync);
}

void BaseWindow::pkPre(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_PRE",
        "pk_status": 101,
        "pk_id": 100970480,
        "timestamp": 1607763991,
        "data": {
            "battle_type": 1, // 自己开始匹配？
            "uname": "SLe\\u4e36\\u82cf\\u4e50",
            "face": "http:\\/\\/i2.hdslb.com\\/bfs\\/face\\/4636d48aeefa1a177bc2bdfb595892d3648b80b1.jpg",
            "uid": 13330958,
            "room_id": 271270,
            "season_id": 30,
            "pre_timer": 10,
            "pk_votes_name": "\\u4e71\\u6597\\u503c",
            "end_win_task": null
        },
        "roomid": 22532956
    }*/

    /*{ 对面匹配过来的情况
        "cmd": "PK_BATTLE_PRE",
        "pk_status": 101,
        "pk_id": 100970387,
        "timestamp": 1607763565,
        "data": {
            "battle_type": 2, // 对面开始匹配？
            "uname": "\\u519c\\u6751\\u9493\\u9c7c\\u5c0f\\u6b66\\u5929\\u5929\\u76f4\\u64ad",
            "face": "http:\\/\\/i0.hdslb.com\\/bfs\\/face\\/fbaa9cfbc214164236cdbe79a77bcaae5334e9ef.jpg",
            "uid": 199775659, // 对面用户ID
            "room_id": 12298098, // 对面房间ID
            "season_id": 30,
            "pre_timer": 10,
            "pk_votes_name": "\\u4e71\\u6597\\u503c", // 乱斗值
            "end_win_task": null
        },
        "roomid": 22532956
    }*/

    QJsonObject data = json.value("data").toObject();
    QString uname = data.value("uname").toString();
    QString uid = QString::number(static_cast<qint64>(data.value("uid").toDouble()));
    QString room_id = QString::number(static_cast<qint64>(data.value("room_id").toDouble()));
    pkUname = uname;
    pkRoomId = room_id;
    pkUid = uid;

    qDebug() << "准备大乱斗，已匹配：" << static_cast<qint64>(json.value("pk_id").toDouble());
    if (danmakuWindow)
    {
        if (uname.isEmpty())
            danmakuWindow->setStatusText("大乱斗匹配中...");
        else if (!pkRoomId.isEmpty())
        {
            int pkCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
            QString text = "匹配：" + uname;
            if(pkCount > 0)
                text += "[" + QString::number(pkCount) + "]";
            danmakuWindow->setStatusText(text);
            qDebug() << "主播：" << uname << pkUid << pkRoomId;
            danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
        }
    }
    pkToLive = QDateTime::currentSecsSinceEpoch();

    if (pkChuanmenEnable /*&& battle_type == 2*/)
    {
        connectPkRoom();
    }
    ui->actionShow_PK_Video->setEnabled(true);
    ui->actionJoin_Battle->setEnabled(false);
    pkGifts.clear();

    // 处理PK对面直播间事件
    if (hasEvent("PK_MATCH_INFO"))
    {
        getPkMatchInfo();
    }
}

void BaseWindow::pkStart(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_START",
        "data": {
            "battle_type": 1, // 不知道其他类型是啥
            "final_hit_votes": 0,
            "pk_end_time": 1605748342,
            "pk_frozen_time": 1605748332,
            "pk_start_time": 1605748032,
            "pk_votes_add": 0,
            "pk_votes_name": "乱斗值",
            "pk_votes_type": 0
        },
        "pk_id": 100729281,
        "pk_status": 201,
        "timestamp": 1605748032
    }*/

    QJsonObject data = json.value("data").toObject();
    pking = true;
    qint64 startTime = static_cast<qint64>(data.value("pk_start_time").toDouble());
    // qint64 endTime = static_cast<qint64>(data.value("pk_end_time").toDouble());
    pkEndTime = startTime + 300; // 因为endTime要延迟10秒，还是用startTime来判断吧
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 deltaEnd = pkEndTime - currentTime;
    QString roomId = this->roomId;
    oppositeTouta = 0;
    cmAudience.clear();
    int battle_type = data.value("battle_type").toInt();
    if (battle_type == 1) // 普通大乱斗
        pkVideo = false;
    else if (battle_type == 2) // 视频大乱斗
        pkVideo = true;
    else
        pkVideo = false;
    if (pkVideo)
    {
        pkToLive = currentTime;
        qDebug() << "开始视频大乱斗";
    }

    // 结束后
    QTimer::singleShot(deltaEnd, [=]{
        pkEnding = false;
        pkVoting = 0;
    });

    // 结束前2秒
    QTimer::singleShot(deltaEnd*1000 - pkJudgeEarly, [=]{
        if (!pking || roomId != this->roomId) // 比如换房间了
        {
            qDebug() << "大乱斗结束前，逻辑不正确" << pking << roomId
                     << QDateTime::currentSecsSinceEpoch() << pkEndTime;
            return ;
        }
        slotPkEnding();
    });

    pkTimer->start();
    if (danmakuWindow)
    {
        danmakuWindow->showStatusText();
        danmakuWindow->setToolTip(pkUname);
        danmakuWindow->setPkStatus(1, pkRoomId.toLongLong(), pkUid.toLongLong(), pkUname);
    }
    qint64 pkid = static_cast<qint64>(json.value("pk_id").toDouble());
    qDebug() << "开启大乱斗, id =" << pkid << "  room=" << pkRoomId << "  user=" << pkUid << "   battle_type=" << battle_type;

    // 1605757123 1605757123 1605757433 时间测试
    // qDebug() << QDateTime::currentSecsSinceEpoch() << startTime << endTime;

    // 保存PK信息
    int pkCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt();
    danmakuCounts->setValue("pk/" + pkRoomId, pkCount+1);
    qDebug() << "保存匹配次数：" << pkRoomId << pkUname << (pkCount+1);

    // PK提示
    QString text = "开启大乱斗：" + pkUname;
    if (pkCount)
        text += "  PK过" + QString::number(pkCount) + "次";
    else
        text += "  初次匹配";
    localNotify(text, pkUid.toLongLong());

    // 处理对面直播姬界面
    if (hasEvent("PK_MATCH_ONLINE_GUARD"))
    {
        getPkOnlineGuardPage(0);
    }
}

void BaseWindow::pkProcess(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_PROCESS",
        "data": {
            "battle_type": 1,
            "init_info": {
                "best_uname": "我今天超可爱0",
                "room_id": 22532956,
                "votes": 132
            },
            "match_info": {
                "best_uname": "银河的偶尔限定女友粉",
                "room_id": 21398069,
                "votes": 156
            }
        },
        "pk_id": 100729411,
        "pk_status": 201,
        "timestamp": 1605749908
    }*/
    QJsonObject data = json.value("data").toObject();
    int prevMyVotes = myVotes;
    int prevMatchVotes = matchVotes;
    if (snum(static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble())) == roomId)
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
    }
    else
    {
        myVotes = data.value("match_info").toObject().value("votes").toInt();
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
    }

    if (!pkTimer->isActive())
        pkTimer->start();

    if (pkEnding)
    {
        qDebug() << "大乱斗进度(偷塔阶段)：" << myVotes << matchVotes << "   等待送到：" << pkVoting;
        int maxGold = getPkMaxGold(qMax(myVotes, matchVotes));

        // 显示偷塔情况
        if (prevMyVotes < myVotes)
        {
            int delta = myVotes - prevMyVotes;
            localNotify("[己方偷塔] + " + snum(delta));

            // B站返回的规则改了，偷塔的时候获取不到礼物了
            pkVoting -= delta;
            if (pkVoting < 0)
                pkVoting = 0;
        }
        if (prevMatchVotes < matchVotes)
        {
            oppositeTouta++;
            localNotify("[对方偷塔] + " + snum(matchVotes - prevMatchVotes));

            {
                int melon = 100 / goldTransPk; // 单个吃瓜有多少乱斗值
                int num = static_cast<int>((matchVotes-myVotes-pkVoting+melon)/melon);
                QString s = QString("myVotes:%1, pkVoting:%2, matchVotes:%3, maxGold:%4, goldTransPk:%5, oppositeTouta:%6, need:%7")
                            .arg(myVotes).arg(pkVoting).arg(matchVotes).arg(maxGold).arg(goldTransPk).arg(oppositeTouta)
                            .arg(num);
                qDebug() << s;
                if (danmuLogStream)
                {
                    (*danmuLogStream) << s << "\n";
                    (*danmuLogStream).flush(); // 立刻刷新到文件里
                }
            }
        }

        // 反偷塔，防止对方也在最后几秒刷礼物
        if (ui->pkAutoMelonCheck->isChecked()
                && myVotes + pkVoting <= matchVotes && myVotes + pkVoting + maxGold/goldTransPk > matchVotes
                /* && oppositeTouta < 6 // 对面之前未连续偷塔（允许被偷塔五次）（可能是连刷，这时候几个吃瓜偷塔没用） */
                && !toutaBlankList.contains(pkRoomId) && !magicalRooms.contains(pkRoomId))
        {
            // 调用送礼
            int melon = 100 / goldTransPk; // 单个吃瓜有多少乱斗值
            int num = static_cast<int>((matchVotes-myVotes-pkVoting+melon)/melon);
            sendGift(20004, num);
            localNotify("[反偷塔] " + snum(matchVotes-myVotes-pkVoting+1) + "，赠送 " + snum(num) + " 个吃瓜");
            pkVoting += melon * num;
            qDebug() << "大乱斗再次赠送" << num << "个吃瓜：" << myVotes << "vs" << matchVotes;
            toutaCount++;
            chiguaCount += num;
            saveTouta();
        }
    }
    else
    {
        qDebug() << "大乱斗进度：" << myVotes << matchVotes;
    }
}

void BaseWindow::pkEnd(QJsonObject json)
{
    /*{
        "cmd": "PK_BATTLE_END",
        "data": {
            "battle_type": 1,
            "init_info": {
                "best_uname": "我今天超可爱0",
                "room_id": 22532956,
                "votes": 10,
                "winner_type": 2
            },
            "match_info": {
                "best_uname": "",
                "room_id": 22195813,
                "votes": 0,
                "winner_type": -1
            },
            "timer": 10
        },
        "pk_id": "100729259",
        "pk_status": 401,
        "timestamp": 1605748006
    }*/
    // winner_type: 2赢，-1输，两边2平局

    QJsonObject data = json.value("data").toObject();
    if (pkVideo)
        pkToLive = QDateTime::currentSecsSinceEpoch();
    int winnerType1 = data.value("init_info").toObject().value("winner_type").toInt();
    int winnerType2 = data.value("match_info").toObject().value("winner_type").toInt();
    qint64 thisRoomId = static_cast<qint64>(data.value("init_info").toObject().value("room_id").toDouble());
    if (pkTimer)
        pkTimer->stop();
    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setToolTip("");
        danmakuWindow->setPkStatus(0, 0, 0, "");
    }
    QString bestName = "";
    int winnerType = 0;
    if (snum(thisRoomId) == roomId) // init是自己
    {
        myVotes = data.value("init_info").toObject().value("votes").toInt();
        matchVotes = data.value("match_info").toObject().value("votes").toInt();
        bestName = data.value("init_info").toObject().value("best_uname").toString();
        winnerType = winnerType1;
    }
    else // match是自己
    {
        matchVotes = data.value("init_info").toObject().value("votes").toInt();
        myVotes = data.value("match_info").toObject().value("votes").toInt();
        bestName = data.value("match_info").toObject().value("best_uname").toString();
        winnerType = winnerType2;
    }

    bool ping = winnerType1 == winnerType2;
    bool result = winnerType > 0;

    qint64 bestUid = 0;
    int winCode = 0;
    if (!ping)
        winCode = winnerType;
    if (myVotes > 0)
    {
        for (int i = pkGifts.size()-1; i >= 0; i--)
            if (pkGifts.at(i).getNickname() == bestName)
            {
                bestUid = pkGifts.at(i).getUid();
                break;
            }
        LiveDanmaku danmaku(bestName, bestUid, winCode, myVotes);
        triggerCmdEvent("PK_BEST_UNAME", danmaku);
    }
    triggerCmdEvent("PK_END", LiveDanmaku(bestName, bestUid, winCode, myVotes));

    localNotify(QString("大乱斗 %1：%2 vs %3")
                                     .arg(ping ? "平局" : (result ? "胜利" : "失败"))
                                     .arg(myVotes)
                                     .arg(matchVotes));
    qDebug() << "大乱斗结束，结果：" << (ping ? "平局" : (result ? "胜利" : "失败")) << myVotes << matchVotes;
    myVotes = 0;
    matchVotes = 0;
    QTimer::singleShot(60000, [=]{
        if (pking) // 下一把PK，已经清空了
            return ;
        cmAudience.clear();
    });

    // 保存对面偷塔次数
    if (oppositeTouta && !pkUname.isEmpty())
    {
        int count = danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
        danmakuCounts->setValue("touta/" + pkRoomId, count+1);
    }

    // 清空大乱斗数据
    pking = false;
    pkEnding = false;
    pkVoting = 0;
    pkEndTime = 0;
    pkUname = "";
    pkUid = "";
    pkRoomId = "";
    myAudience.clear();
    oppositeAudience.clear();
    pkVideo = false;
    ui->actionShow_PK_Video->setEnabled(false);

    if (cookieUid == upUid)
        ui->actionJoin_Battle->setEnabled(true);

    if (pkSocket)
    {
        try {
            if (pkSocket->state() == QAbstractSocket::ConnectedState)
                pkSocket->close(); // 会自动deleterLater
            // pkSocket->deleteLater();
        } catch (...) {
            qCritical() << "delete pkSocket failed";
        }
        pkSocket = nullptr;
    }
}

int BaseWindow::getPkMaxGold(int votes)
{
    if (!ui->pkAutoMaxGoldCheck->isChecked())
        return pkMaxGold;
    int money = qMax(0, votes / (1000 / goldTransPk) - pkMaxGold * 10 / 1000);
    double prop = pow(money, 1.0/3);
    double maxProp = 10.0 / qMax(1, pkMaxGold / 1000);
    maxProp = qMax(1.0, maxProp);
    prop = qMax(1.0, prop);
    prop = qMin(prop, maxProp);
    if (ui->pkAutoMelonCheck->isChecked() && debugPrint)
        localNotify("[偷塔上限 " + snum(votes) + " => " + snum(int(pkMaxGold * prop)) + "金瓜子, "
                    +QString::number(pow(money, 1.0/3), 'f', 1)+"倍]");
    return int(pkMaxGold * prop);
}

void BaseWindow::getRoomCurrentAudiences(QString roomId, QSet<qint64> &audiences)
{
    QString url = "https://api.live.bilibili.com/ajax/msg";
    QStringList param{"roomid", roomId};
    connect(new NetUtil(url, param), &NetUtil::finished, this, [&](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "getRoomCurrentAudiences.ERROR:" << error.errorString();
            qCritical() << result;
            return ;
        }
        QJsonObject json = document.object();
        QJsonArray danmakus = json.value("data").toObject().value("room").toArray();
//        qDebug() << "初始化房间" << roomId << "观众：";
        for (int i = 0; i < danmakus.size(); i++)
        {
            LiveDanmaku danmaku = LiveDanmaku::fromDanmakuJson(danmakus.at(i).toObject());
//            if (!audiences.contains(danmaku.getUid()))
//                qDebug() << "    添加观众：" << danmaku.getNickname();
            audiences.insert(danmaku.getUid());
        }
    });
}

void BaseWindow::connectPkRoom()
{
    if (pkRoomId.isEmpty())
        return ;

    // 根据弹幕消息
    myAudience.clear();
    oppositeAudience.clear();

    getRoomCurrentAudiences(roomId, myAudience);
    getRoomCurrentAudiences(pkRoomId, oppositeAudience);

    // 额外保存的许多本地弹幕消息
    for (int i = 0; i < roomDanmakus.size(); i++)
    {
        qint64 uid = roomDanmakus.at(i).getUid();
        if (uid)
            myAudience.insert(uid);
    }

    // 保存自己主播、对面主播（带头串门？？？）
    myAudience.insert(upUid.toLongLong());
    oppositeAudience.insert(pkUid.toLongLong());

    // 连接socket
    if (pkSocket)
        pkSocket->deleteLater();

    pkSocket = new QWebSocket();

    connect(pkSocket, &QWebSocket::connected, this, [=]{
        SOCKET_DEB << "pkSocket connected";
        // 5秒内发送认证包
        sendVeriPacket(pkSocket, pkRoomId, pkToken);
    });

    connect(pkSocket, &QWebSocket::disconnected, this, [=]{
        // 正在直播的时候突然断开了
        if (isLiving() && pkSocket)
        {
            pkSocket->deleteLater();
            pkSocket = nullptr;
            return ;
        }
    });

    connect(pkSocket, &QWebSocket::binaryMessageReceived, this, [=](const QByteArray &message){
        slotPkBinaryMessageReceived(message);
    });

    // ========== 开始连接 ==========
    QString url = "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo";
    url += "?id="+pkRoomId+"&type=0";
    QNetworkAccessManager* manager = new QNetworkAccessManager;
    QNetworkRequest* request = new QNetworkRequest(url);
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        QByteArray dataBa = reply->readAll();
        manager->deleteLater();
        delete request;
        reply->deleteLater();

        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(dataBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << "获取弹幕信息出错：" << error.errorString();
            return ;
        }
        QJsonObject json = document.object();

        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("pk返回结果不为0：") << json.value("message").toString();
            return ;
        }

        QJsonObject data = json.value("data").toObject();
        pkToken = data.value("token").toString();
        QJsonArray hostArray = data.value("host_list").toArray();
        QString link = hostArray.first().toObject().value("host").toString();
        int port = hostArray.first().toObject().value("wss_port").toInt();
        QString host = QString("wss://%1:%2/sub").arg(link).arg(port);
        SOCKET_DEB << "pk.socket.host:" << host;

        // ========== 连接Socket ==========
        QSslConfiguration config = pkSocket->sslConfiguration();
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        config.setProtocol(QSsl::TlsV1SslV3);
        pkSocket->setSslConfiguration(config);
        pkSocket->open(host);
    });
    manager->get(*request);
}

void BaseWindow::slotPkBinaryMessageReceived(const QByteArray &message)
{
    int operation = ((uchar)message[8] << 24)
            + ((uchar)message[9] << 16)
            + ((uchar)message[10] << 8)
            + ((uchar)message[11]);
    QByteArray body = message.right(message.length() - 16);
    SOCKET_INF << "pk操作码=" << operation << "  正文=" << (body.left(35)) << "...";

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(body, &error);
    QJsonObject json;
    if (error.error == QJsonParseError::NoError)
        json = document.object();

    if (operation == SEND_MSG_REPLY) // 普通包
    {
        QString cmd;
        if (!json.isEmpty())
        {
            cmd = json.value("cmd").toString();
            qDebug() << "pk普通CMD：" << cmd;
            SOCKET_INF << json;
        }

        if (cmd == "NOTICE_MSG") // 全站广播（不用管）
        {
        }
        else if (cmd == "ROOM_RANK")
        {
        }
        else // 压缩弹幕消息
        {
            short protover = (message[6]<<8) + message[7];
            SOCKET_INF << "pk协议版本：" << protover;
            if (protover == 2) // 默认协议版本，zlib解压
            {
                uncompressPkBytes(body);
            }
        }
    }

//    delete[] body.data();
//    delete[] message.data();
    SOCKET_DEB << "PkSocket消息处理结束";
}

void BaseWindow::showWidget(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason)
    {
    case QSystemTrayIcon::Trigger:
        if (!this->isHidden())
            this->hide();
        else
        {
            this->showNormal();
            this->activateWindow();
        }
        break;
    case QSystemTrayIcon::MiddleClick:
        on_actionShow_Live_Danmaku_triggered();
        break;
    default:
        break;
    }
}

void BaseWindow::uncompressPkBytes(const QByteArray &body)
{
    QByteArray unc = zlibToQtUncompr(body.data(), body.size()+1);
    int offset = 0;
    short headerSize = 16;
    while (offset < unc.size() - headerSize)
    {
        int packSize = ((uchar)unc[offset+0] << 24)
                + ((uchar)unc[offset+1] << 16)
                + ((uchar)unc[offset+2] << 8)
                + (uchar)unc[offset+3];
        QByteArray jsonBa = unc.mid(offset + headerSize, packSize - headerSize);
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(jsonBa, &error);
        if (error.error != QJsonParseError::NoError)
        {
            qCritical() << s8("pk解析解压后的JSON出错：") << error.errorString();
            qCritical() << s8("pk包数值：") << offset << packSize << "  解压后大小：" << unc.size();
            qCritical() << s8(">>pk当前JSON") << jsonBa;
            qCritical() << s8(">>pk解压正文") << unc;
            return ;
        }
        QJsonObject json = document.object();
        QString cmd = json.value("cmd").toString();
        SOCKET_INF << "pk解压后获取到CMD：" << cmd;
        if (cmd != "ROOM_BANNER" && cmd != "ACTIVITY_BANNER_UPDATE_V2" && cmd != "PANEL"
                && cmd != "ONLINERANK")
            SOCKET_INF << "pk单个JSON消息：" << offset << packSize << QString(jsonBa);
        handlePkMessage(json);

        offset += packSize;
    }
}

void BaseWindow::handlePkMessage(QJsonObject json)
{
    QString cmd = json.value("cmd").toString();
    qDebug() << s8(">pk消息命令：") << cmd;
    if (cmd == "DANMU_MSG") // 收到弹幕
    {
        if (!pkMsgSync || (pkMsgSync == 1 && !pkVideo))
            return ;
        QJsonArray info = json.value("info").toArray();
        QJsonArray array = info[0].toArray();
        qint64 textColor = array[3].toInt(); // 弹幕颜色
        qint64 timestamp = static_cast<qint64>(array[4].toDouble()); // 13位
        QString msg = info[1].toString();
        QJsonArray user = info[2].toArray();
        qint64 uid = static_cast<qint64>(user[0].toDouble());
        QString username = user[1].toString();
        QString unameColor = user[7].toString();
        int level = info[4].toArray()[0].toInt();
        QJsonArray medal = info[3].toArray();
        int medal_level = 0;

        bool toView = pking &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() && medal.size() >= 4 &&
                     snum(static_cast<qint64>(medal[3].toDouble())) == roomId));

        // !弹幕的时间戳是13位，其他的是10位！
        qDebug() << s8("pk接收到弹幕：") << username << msg << QDateTime::fromMSecsSinceEpoch(timestamp);

        // 添加到列表
        QString cs = QString::number(textColor, 16);
        while (cs.size() < 6)
            cs = "0" + cs;
        LiveDanmaku danmaku(username, msg, uid, level, QDateTime::fromMSecsSinceEpoch(timestamp),
                                                 unameColor, "#"+cs);
        if (medal.size() >= 4)
        {
            medal_level = medal[0].toInt();
            danmaku.setMedal(snum(static_cast<qint64>(medal[3].toDouble())),
                    medal[1].toString(), medal_level, medal[2].toString());
        }
        /*if (noReplyMsgs.contains(msg) && snum(uid) == cookieUid)
        {
            danmaku.setNoReply();
            noReplyMsgs.removeOne(msg);
        }
        else
            minuteDanmuPopular++;*/
        danmaku.setToView(toView);
        danmaku.setPkLink(true);
        appendNewLiveDanmaku(danmaku);

        triggerCmdEvent("PK_" + cmd, danmaku);
    }
    else if (cmd == "SEND_GIFT") // 有人送礼
    {
        if (!pkMsgSync || (pkMsgSync == 1 && !pkVideo))
            return ;
        QJsonObject data = json.value("data").toObject();
        int giftId = data.value("giftId").toInt();
        int giftType = data.value("giftType").toInt(); // 不知道是啥，金瓜子1，银瓜子（小心心、辣条）5？
        QString giftName = data.value("giftName").toString();
        QString username = data.value("uname").toString();
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        int num = data.value("num").toInt();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble()); // 秒
        timestamp = QDateTime::currentSecsSinceEpoch(); // *不管送出礼物的时间，只管机器人接收到的时间
        QString coinType = data.value("coin_type").toString();
        int totalCoin = data.value("total_coin").toInt();

        qDebug() << s8("接收到送礼：") << username << giftId << giftName << num << s8("  总价值：") << totalCoin << coinType;
        QString localName = getLocalNickname(uid);
        /*if (!localName.isEmpty())
            username = localName;*/
        LiveDanmaku danmaku(username, giftId, giftName, num, uid, QDateTime::fromSecsSinceEpoch(timestamp), coinType, totalCoin);
        if (!data.value("medal_info").isNull())
        {
            QJsonObject medalInfo = data.value("medal_info").toObject();
            QString anchorRoomId = snum(qint64(medalInfo.value("anchor_room_id").toDouble())); // !注意：这个一直为0！
            QString anchorUname = medalInfo.value("anchor_uname").toString(); // !注意：也是空的
            int guardLevel = medalInfo.value("guard_level").toInt();
            int isLighted = medalInfo.value("is_lighted").toInt();
            int medalColor = medalInfo.value("medal_color").toInt();
            int medalColorBorder = medalInfo.value("medal_color_border").toInt();
            int medalColorEnd = medalInfo.value("medal_color_end").toInt();
            int medalColorStart = medalInfo.value("medal_color_start").toInt();
            int medalLevel = medalInfo.value("medal_level").toInt();
            QString medalName = medalInfo.value("medal_name").toString();
            QString spacial = medalInfo.value("special").toString();
            QString targetId = snum(qint64(medalInfo.value("target_id").toDouble())); // 目标用户ID
            if (!medalName.isEmpty())
            {
                QString cs = QString::number(medalColor, 16);
                while (cs.size() < 6)
                    cs = "0" + cs;
                danmaku.setMedal(anchorRoomId, medalName, medalLevel, cs, anchorUname);
            }
        }

        danmaku.setPkLink(true);
        // appendNewLiveDanmaku(danmaku);

        triggerCmdEvent("PK_" + cmd, danmaku);
    }
    else if (cmd == "INTERACT_WORD")
    {
        if (!pkChuanmenEnable) // 可能是中途关了
            return ;
        QJsonObject data = json.value("data").toObject();
        int msgType = data.value("msg_type").toInt(); // 1进入直播间，2关注，3分享直播间，4特别关注
        qint64 uid = static_cast<qint64>(data.value("uid").toDouble());
        QString username = data.value("uname").toString();
        qint64 timestamp = static_cast<qint64>(data.value("timestamp").toDouble());
        bool isadmin = data.value("isadmin").toBool();
        QString unameColor = data.value("uname_color").toString();
        bool isSpread = data.value("is_spread").toBool();
        QString spreadDesc = data.value("spread_desc").toString();
        QString spreadInfo = data.value("spread_info").toString();
        QJsonObject fansMedal = data.value("fans_medal").toObject();
        QString roomId = snum(qint64(data.value("room_id").toDouble()));
        bool toView = pking &&
                ((!oppositeAudience.contains(uid) && myAudience.contains(uid))
                 || (!pkRoomId.isEmpty() &&
                     snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())) == this->roomId));
        bool attentionToMyRoom = false;
        if (!toView) // 不是自己方过去串门的
        {
            if (roomId == this->roomId && msgType == 2) // 在对面关注当前主播
                attentionToMyRoom = true;
            else
                if (!pkMsgSync || (pkMsgSync == 1 && !pkVideo))
                    return ;
        }
        if (toView || attentionToMyRoom)
        {
            if (!cmAudience.contains(uid))
                cmAudience.insert(uid, timestamp);
        }

        // qDebug() << s8("pk观众互动：") << username << spreadDesc;
        QString localName = getLocalNickname(uid);
        LiveDanmaku danmaku(username, uid, QDateTime::fromSecsSinceEpoch(timestamp), isadmin,
                            unameColor, spreadDesc, spreadInfo);
        danmaku.setMedal(snum(static_cast<qint64>(fansMedal.value("anchor_roomid").toDouble())),
                         fansMedal.value("medal_name").toString(),
                         fansMedal.value("medal_level").toInt(),
                         QString("#%1").arg(fansMedal.value("medal_color").toInt(), 6, 16, QLatin1Char('0')),
                         "");
        danmaku.setToView(toView);
        danmaku.setPkLink(true);

        if (attentionToMyRoom)
        {
            danmaku.transToAttention(timestamp);
            localNotify("对面的 " + username + " 关注了本直播间", uid);
            triggerCmdEvent("ATTENTION_ON_OPPOSITE", danmaku);
        }
        else if (msgType == 1)
        {
            if (toView)
            {
                localNotify(username + " 跑去对面串门", uid); // 显示一个短通知，就不作为一个弹幕了
                triggerCmdEvent("CALL_ON_OPPOSITE", danmaku);
            }
            triggerCmdEvent("PK_" + cmd, danmaku);
        }
        else if (msgType == 2)
        {
            danmaku.transToAttention(timestamp);
            if (toView)
            {
                localNotify(username + " 关注了对面直播间", uid); // XXX
                triggerCmdEvent("ATTENTION_OPPOSITE", danmaku);
            }
            triggerCmdEvent("PK_ATTENTION", danmaku);
        }
        else if (msgType == 3)
        {
            danmaku.transToShare();
            if (toView)
            {
                localNotify(username + " 分享了对面直播间", uid); // XXX
                triggerCmdEvent("SHARE_OPPOSITE", danmaku);
            }
            triggerCmdEvent("PK_SHARE", danmaku);
        }
        // appendNewLiveDanmaku(danmaku);
    }
    else if (cmd == "CUT_OFF")
    {
        localNotify("对面直播间被超管切断");
    }
}

bool BaseWindow::shallAutoMsg() const
{
    return !ui->sendAutoOnlyLiveCheck->isChecked() || (isLiving() /*&& popularVal > 1*/);
}

bool BaseWindow::shallAutoMsg(const QString &sl) const
{
    if (sl.contains("%living%"))
        return true;
    return shallAutoMsg();
}

bool BaseWindow::shallAutoMsg(const QString &sl, bool &manual)
{
    if (sl.contains("%living%"))
    {
        manual = true;
        return true;
    }
    return shallAutoMsg();
}

void BaseWindow::addBannedWord(QString word, QString anchor)
{
    if (word.isEmpty())
        return ;

    QString text = ui->autoBlockNewbieKeysEdit->toPlainText();
    if (anchor.isEmpty())
    {
        if (anchor.endsWith("|") || word.startsWith("|"))
            text += word;
        else
            text += "|" + word;
    }
    else
    {
        if (!anchor.startsWith("|"))
            anchor = "|" + anchor;
        text.replace(anchor, "|" + word + anchor);
    }
    ui->autoBlockNewbieKeysEdit->setPlainText(text);

    text = ui->promptBlockNewbieKeysEdit->toPlainText();

    if (anchor.isEmpty())
    {
        if (anchor.endsWith("|") || word.startsWith("|"))
            text += word;
        else
            text += "|" + word;
    }
    else
    {
        if (!anchor.startsWith("|"))
            anchor = "|" + anchor;
        text.replace(anchor, "|" + word + anchor);
    }
    ui->promptBlockNewbieKeysEdit->setPlainText(text);
}

void BaseWindow::saveMonthGuard()
{
    QDir dir(dataPath + "guard_month");
    dir.mkpath(dir.absolutePath());
    QDate date = QDate::currentDate();
    QString fileName = QString("%1_%2-%3.csv").arg(roomId).arg(date.year()).arg(date.month());
    QString filePath = dir.absoluteFilePath(dir.absoluteFilePath(fileName));

    QFile file(filePath);
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    stream << QString("UID,昵称,级别,备注\n").toUtf8();
    auto getGuardName = [=](int level) {
        if (level == 1)
            return QString("总督").toUtf8();
        else if (level == 2)
            return QString("提督").toUtf8();
        return QString("舰长").toUtf8();
    };
    for (int i = 0; i < guardInfos.size(); i++)
    {
        LiveDanmaku danmaku = guardInfos.at(i);
        stream << danmaku.getUid() << ","
               << danmaku.getNickname() << ","
               << getGuardName(danmaku.getGuard()) << "\n";
    }

    file.close();
}

void BaseWindow::saveEveryGuard(LiveDanmaku danmaku)
{
    QDir dir(dataPath + "guard_histories");
    dir.mkpath(dir.absolutePath());
    QString filePath = dir.absoluteFilePath(dir.absoluteFilePath(roomId + ".csv"));

    QFile file(filePath);
    bool exists = file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        qDebug() << "打开上船记录文件失败：" << filePath;
    QTextStream stream(&file);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    if (!exists)
        stream << QString("日期,时间,昵称,礼物,数量,累计,UID,备注\n").toUtf8();

    stream << danmaku.getTimeline().toString("yyyy-MM-dd") << ","
           << danmaku.getTimeline().toString("hh:mm") << ","
           << danmaku.getNickname() << ","
           << danmaku.getGiftName() << ","
           << danmaku.getNumber() << ","
           << danmakuCounts->value("guard/" + snum(danmaku.getUid()), 0).toInt() << ","
           << danmaku.getUid() << ","
           << userMarks->value("base/" + snum(danmaku.getUid()), "").toString() << "\n";

    file.close();
}

void BaseWindow::saveEveryGift(LiveDanmaku danmaku)
{
    QDir dir(dataPath + "gift_histories");
    dir.mkpath(dir.absolutePath());
    QDate date = QDate::currentDate();
    QString fileName = QString("%1_%2-%3.csv").arg(roomId).arg(date.year()).arg(date.month());
    QString filePath = dir.absoluteFilePath(dir.absoluteFilePath(fileName));

    QFile file(filePath);
    bool exists = file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        qDebug() << "打开礼物记录文件失败：" << filePath;
    QTextStream stream(&file);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    if (!exists)
        stream << QString("日期,时间,昵称,礼物,数量,金瓜子,UID\n").toUtf8();

    stream << danmaku.getTimeline().toString("yyyy-MM-dd") << ","
           << danmaku.getTimeline().toString("hh:mm") << ","
           << danmaku.getNickname() << ","
           << danmaku.getGiftName() << ","
           << danmaku.getNumber() << ","
           << (danmaku.isGoldCoin() ? danmaku.getTotalCoin() : 0) << ","
           << danmaku.getUid() << "\n";

    file.close();
}

void BaseWindow::appendFileLine(QString dirName, QString fileName, QString format, LiveDanmaku danmaku)
{
    if (dirName.startsWith("/"))
        dirName.replace(0, 1, "");
    QDir dir(dataPath + dirName);
    dir.mkpath(dir.absolutePath());
    QString filePath = dir.absoluteFilePath(dir.absoluteFilePath(fileName));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        qDebug() << "打开文件失败：" << filePath;
    QTextStream stream(&file);
    if (!codeFileCodec.isEmpty())
        stream.setCodec(codeFileCodec.toUtf8());

    stream << processDanmakuVariants(format, danmaku) << "\n";

    file.close();
}

void BaseWindow::releaseLiveData(bool prepare)
{
    ui->roomRankLabel->setText("");
    ui->roomRankLabel->setToolTip("");

    if (!prepare) // 切换房间或者断开连接
    {
        pking = false;
        pkUid = "";
        pkUname = "";
        pkRoomId = "";
        pkEnding = false;
        pkVoting = 0;
        pkVideo = false;
        pkTimer->stop();
        pkEndTime = 0;
        myAudience.clear();
        oppositeAudience.clear();
        fansList.clear();
        currentGuards.clear();
        guardInfos.clear();
        currentFans = 0;
        currentFansClub = 0;

        autoMsgQueues.clear();
        for (int i = 0; i < CHANNEL_COUNT; i++)
            msgCds[i] = 0;
        roomDanmakus.clear();
        pkGifts.clear();

        finishSaveDanmuToFile();

        if (ui->saveRecvCmdsCheck->isChecked())
        {
            ui->saveRecvCmdsCheck->setChecked(false);
            on_saveRecvCmdsCheck_clicked();
        }

        if (pushCmdsFile)
        {
            on_pushRecvCmdsButton_clicked();
        }
    }
    else // 下播，依旧保持连接
    {

    }

    danmuPopulQueue.clear();
    minuteDanmuPopul = 0;
    danmuPopulValue = 0;

    diangeHistory.clear();
    ui->diangeHistoryListWidget->clear();

    statusLabel->setText("");
    rankLabel->setText("");
    rankLabel->setToolTip("");
    fansLabel->setText("");
    popularVal = 0;

    liveTimestamp = QDateTime::currentMSecsSinceEpoch();
    xliveHeartBeatTimer->stop();

    // 本次直播数据
    liveAllGifts.clear();
    liveAllGuards.clear();

    if (danmakuWindow)
    {
        danmakuWindow->hideStatusText();
        danmakuWindow->setIds(0, 0);
        danmakuWindow->releaseLiveData(prepare);
    }

    if (pkSocket)
    {
        if (pkSocket->state() == QAbstractSocket::ConnectedState)
            pkSocket->close();
        pkSocket = nullptr;
    }

    ui->actionShow_Live_Video->setEnabled(false);
    ui->actionShow_PK_Video->setEnabled(false);
    ui->actionJoin_Battle->setEnabled(false);

    finishLiveRecord();
    saveCalculateDailyData();

    QPixmap face = roomId.isEmpty() ? QPixmap() : upFace;
    setWindowIcon(face);
    tray->setIcon(face);

    // 清理一周没来的用户
    danmakuCounts->beginGroup("comeTime");
    QStringList removedKeys;
    auto keys = danmakuCounts->allKeys();
    qint64 week = QDateTime::currentSecsSinceEpoch() - 7 * 24 * 3600;
    foreach (auto key, keys)
    {
        qint64 value = danmakuCounts->value(key).toLongLong();
        if (value < week)
        {
            removedKeys.append(key);
        }
    }
    danmakuCounts->endGroup();

    danmakuCounts->beginGroup("comeTime");
    foreach (auto key, removedKeys)
    {
        danmakuCounts->remove(key);
    }
    danmakuCounts->endGroup();

    danmakuCounts->beginGroup("come");
    foreach (auto key, removedKeys)
    {
        danmakuCounts->remove(key);
    }
    danmakuCounts->endGroup();
    danmakuCounts->sync();
}

QRect BaseWindow::getScreenRect()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->availableVirtualGeometry();
    return screenRect;
}

QPixmap BaseWindow::toRoundedPixmap(QPixmap pixmap, int radius) const
{
    QPixmap tmp(pixmap.size());
    tmp.fill(Qt::transparent);
    QPainter painter(&tmp);
    painter.setRenderHints(QPainter::Antialiasing, true);
    painter.setRenderHints(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, tmp.width(), tmp.height(), radius, radius);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, tmp.width(), tmp.height(), pixmap);
    return tmp;
}

void BaseWindow::switchMedalTo(qint64 targetRoomId)
{
    QString url = "https://api.live.bilibili.com/fans_medal/v1/FansMedal/get_list_in_room";
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }

        // 获取用户信息
        QJsonArray medals = json.value("data").toArray();
        /*  buff_msg: "(舰长buff：上限提升至150%)"
            can_delete: false     // 变灰了才能删除
            day_limit: 250000
            guard_level: 3
            guard_type: 3
            icon_code: 2
            icon_text: "最近获得"
            intimacy: 1380
            is_lighted: 1         // 是否未变灰
            is_receive: 1
            last_wear_time: 1613491199
            level: 21
            live_stream_status: 0  // 是否正在直播
            lpl_status: 0
            master_available: 1
            master_status: 0
            medal_color: 1725515
            medal_color_border: 6809855
            medal_color_end: 5414290
            medal_color_start: 1725515
            medal_id: 373753
            medal_level: 21
            medal_name: "181mm"
            next_intimacy: 2000
            rank: "-"
            receive_channel: 4
            receive_time: "2021-01-10 09:33:22"
            room_id: 11584296      // 牌子房间
            score: 50001380
            source: 1
            status: 1              // 是否佩戴中
            sup_code: 2
            sup_text: "最近获得"
            target_face: ""
            target_id: 20285041
            target_name: "懒一夕智能科技"
            today_feed: 0
            today_intimacy: 0
            uid: 20285041          // 牌子用户
        */

        foreach (QJsonValue val, medals)
        {
            QJsonObject medal = val.toObject();
            qint64 roomId = static_cast<qint64>(medal.value("room_id").toDouble());
            int status = medal.value("status").toInt(); // 1佩戴，0未佩戴

            if (roomId == targetRoomId)
            {
                if (status) // 已佩戴，就不用管了
                    return ;

                // 佩带牌子
                /*int isLighted = medal.value("is_lighted").toBool(); // 1能用，0变灰
                if (!isLighted) // 牌子是灰的，可以不用管，发个弹幕就点亮了
                    return ;
                */

                qint64 medalId = static_cast<qint64>(medal.value("medal_id").toDouble());
                wearMedal(medalId);
                return ;
            }
        }
        qDebug() << "未检测到粉丝勋章，无法自动切换";

    });
}

void BaseWindow::wearMedal(qint64 medalId)
{
    QString url("https://api.live.bilibili.com/xlive/web-room/v1/fansMedal/wear");
    QStringList datas;
    datas << "medal_id=" + QString::number(medalId);
    datas << "csrf_token=" + csrf_token;
    datas << "csrf=" + csrf_token;
    datas << "visit_id=";
    QByteArray ba(datas.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            qCritical() << s8("返回结果不为0：") << json.value("message").toString();
            return ;
        }
        qDebug() << "佩戴主播粉丝勋章成功";
    });
}

/**
 * 自动签到
 */
void BaseWindow::doSign()
{
    if (csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("未设置Cookie");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("每日自动签到");
        });
        return ;
    }

    QString url("https://api.live.bilibili.com/xlive/web-ucenter/v1/sign/DoSign");

    // 建立对象
    get(url, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("返回结果不为0：") << msg;
            ui->autoDoSignCheck->setText(msg);
        }
        else
        {
            ui->autoDoSignCheck->setText("签到成功");
            ui->autoDoSignCheck->setToolTip("最近签到时间：" + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss"));
        }
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("每日自动签到");
        });
    });
}

void BaseWindow::joinLOT(qint64 id, bool follow)
{
    if (!id )
        return ;
    if (csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("未设置Cookie");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("自动参与活动");
        });
        return ;
    }

    QString url("https://api.live.bilibili.com/xlive/lottery-interface/v1/Anchor/Join"
             "?id="+QString::number(id)+(follow?"&follow=true":"")+"&platform=pc&csrf_token="+csrf_token+"&csrf="+csrf_token+"&visit_id=");
    qDebug() << "参与天选：" << id << follow << url;

    post(url, QByteArray(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("返回结果不为0：") << msg;
            ui->autoLOTCheck->setText(msg);
            ui->autoLOTCheck->setToolTip(msg);
        }
        else
        {
            ui->autoLOTCheck->setText("参与成功");
            qDebug() << "参与天选成功！";
            ui->autoLOTCheck->setToolTip("最近参与时间：" + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss"));
        }
        QTimer::singleShot(10000, [=]{
            ui->autoLOTCheck->setText("自动参与活动");
        });
    });
}

void BaseWindow::joinStorm(qint64 id)
{
    if (!id )
        return ;
    if (csrf_token.isEmpty())
    {
        ui->autoDoSignCheck->setText("未设置Cookie");
        QTimer::singleShot(10000, [=]{
            ui->autoDoSignCheck->setText("自动参与活动");
        });
        return ;
    }

    QString url("https://api.live.bilibili.com/xlive/lottery-interface/v1/storm/Join"
             "?id="+QString::number(id)+"&color=5566168&csrf_token="+csrf_token+"&csrf="+csrf_token+"&visit_id=");
    qDebug() << "参与节奏风暴：" << id << url;

    post(url, QByteArray(), [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("返回结果不为0：") << msg;
            ui->autoLOTCheck->setText(msg);
            ui->autoLOTCheck->setToolTip(msg);
        }
        else
        {
            ui->autoLOTCheck->setText("参与成功");
            qDebug() << "参与节奏风暴成功！";
            QString content = json.value("data").toObject().value("content").toString();
            ui->autoLOTCheck->setToolTip("最近参与时间：" +
                                            QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss")
                                            + "\n\n" + content);
        }
        QTimer::singleShot(10000, [=]{
            ui->autoLOTCheck->setText("自动参与活动");
        });
    });
}

void BaseWindow::sendPrivateMsg(qint64 uid, QString msg)
{
    if (csrf_token.isEmpty())
    {
        return ;
    }

    QString url("https://api.vc.bilibili.com/web_im/v1/web_im/send_msg");

    QStringList params;
    params << "msg%5Bsender_uid%5D=" + cookieUid;
    params << "msg%5Breceiver_id%5D=" + snum(uid);
    params << "msg%5Breceiver_type%5D=1";
    params << "msg%5Bmsg_type%5D=1";
    params << "msg%5Bmsg_status%5D=0";
    params << "msg%5Bcontent%5D=" + QUrl::toPercentEncoding("{\"content\":\"" + msg + "\"}");
    params << "msg%5Btimestamp%5D="+snum(QDateTime::currentSecsSinceEpoch());
    params << "msg%5Bnew_face_version%5D=0";
    params << "msg%5Bdev_id%5D=81872DC0-FBC0-4CF8-8E93-093DE2083F51";
    params << "from_firework=0";
    params << "build=0";
    params << "mobi_app=web";
    params << "csrf_token=" + csrf_token;
    params << "csrf=" + csrf_token;
    QByteArray ba(params.join("&").toStdString().data());

    // 连接槽
    post(url, ba, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
        {
            QString msg = json.value("message").toString();
            qCritical() << s8("发送消息出错，返回结果不为0：") << msg;
            return ;
        }
    });
}

void BaseWindow::AIReply(qint64 id, QString text, NetStringFunc func, int maxLen, int retry)
{
    if (text.isEmpty())
        return ;

    // 参数信息
    QString url = "https://api.ai.qq.com/fcgi-bin/nlp/nlp_textchat";
    QString nonce_str = "replyAPPKEY";
    QStringList params{"app_id", "2159207490",
                       "nonce_str", nonce_str,
                "question", text,
                "session", snum(id),
                "time_stamp", QString::number(QDateTime::currentSecsSinceEpoch()),
                      };

    // 接口鉴权
    QString pinjie;
    for (int i = 0; i < params.size()-1; i+=2)
        if (!params.at(i+1).isEmpty())
            pinjie += params.at(i) + "=" + QUrl::toPercentEncoding(params.at(i+1)) + "&";
    QString appkey = "sTuC8iS3R9yLNbL9";
    pinjie += "app_key="+appkey;

    QString sign = QString(QCryptographicHash::hash(pinjie.toLocal8Bit(), QCryptographicHash::Md5).toHex().data()).toUpper();
    params << "sign" << sign;
//    qDebug() << pinjie << sign;

    // 获取信息
    connect(new NetUtil(url, params), &NetUtil::finished, this, [=](QString result){
        QJsonParseError error;
        QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            qDebug() << "AI回复：" << error.errorString();
            return ;
        }

        QJsonObject json = document.object();
        if (json.value("ret").toInt() != 0)
        {
            QString msg = json.value("msg").toString();
            qDebug() << "AI回复：" << msg << text;
            if (msg.contains("not found") && retry > 0)
                AIReply(id, text, func, maxLen, retry - 1);
            return ;
        }

        QString answer = json.value("data").toObject().value("answer").toString();

        // 过滤文字
        if (answer.contains("未搜到")
                || answer.isEmpty())
            return ;

        if (answer.length() > maxLen)
            return ;
        func(answer);
    });
}

void BaseWindow::joinBattle(int type)
{
    if (!isLiving() || cookieUid != upUid)
    {
        qCritical() << "未开播或不是主播本人";
        statusLabel->setText("未开播或不是主播本人");
        return ;
    }

    QStringList params{
        "room_id", roomId,
        "platform", "pc",
        "battle_type", snum(type),
        "csrf_token", csrf_token,
        "csrf", csrf_token
    };
    post("https://api.live.bilibili.com/av/v1/Battle/join", params, [=](QJsonObject json){
        if (json.value("code").toInt() != 0)
            statusLabel->setText(json.value("message").toString());
        else if (danmakuWindow)
            danmakuWindow->setStatusText("正在匹配...");
        qDebug() << json;
    });
}

void BaseWindow::detectMedalUpgrade(LiveDanmaku danmaku)
{
    /* {
        "code": 0,
        "msg": "",
        "message": "",
        "data": {
            "guard_type": 3,
            "intimacy": 2672,
            "is_receive": 1,
            "last_wear_time": 1616941910,
            "level": 23,
            "lpl_status": 0,
            "master_available": 1,
            "master_status": 0,
            "medal_id": 37075,
            "medal_name": "蘑菇云",
            "receive_channel": 30726000,
            "receive_time": "2020-12-11 21:41:39",
            "score": 50007172,
            "source": 1,
            "status": 0,
            "target_id": 13908357,
            "today_intimacy": 4,
            "uid": 20285041,
            "target_name": "娇羞的蘑菇",
            "target_face": "https://i1.hdslb.com/bfs/face/180d0e87a0e88cb6c04ce6504c3f04003dd77392.jpg",
            "live_stream_status": 0,
            "icon_code": 0,
            "icon_text": "",
            "rank": "-",
            "medal_color": 1725515,
            "medal_color_start": 1725515,
            "medal_color_end": 5414290,
            "guard_level": 3,
            "medal_color_border": 6809855,
            "is_lighted": 1,
            "today_feed": 4,
            "day_limit": 250000,
            "next_intimacy": 3000,
            "can_delete": false
        }
    } */

    if (upUid.isEmpty() || !danmaku.getTotalCoin()) // 亲密度为0.可能是小心心，不需要判断
    {
        if (debugPrint)
            localNotify("[勋章升级：免费礼物]");
        return ;
    }
    int giftIntimacy = danmaku.getTotalCoin() / 100;
    if (!giftIntimacy) // 0瓜子
    {
        if (danmaku.getGiftId() == 30607 && danmaku.getAnchorRoomid() == roomId && danmaku.getMedalLevel() < 21)
            giftIntimacy = danmaku.getNumber() * 50; // 21级以下的小心心有效，一个50
        else
        {
            if (debugPrint)
                localNotify("[勋章升级：小心心无效]");
            return ;
        }
    }
    QString url = "https://api.live.bilibili.com/fans_medal/v1/fans_medal/get_fans_medal_info?source=1&uid="
            + snum(danmaku.getUid()) + "&target_id=" + upUid;
    get(url, [=](MyJson json){
        MyJson medalObject = json.data();
        if (medalObject.isEmpty())
        {
            if (debugPrint)
                localNotify("[勋章升级：无勋章]");
            return ; // 没有勋章，更没有亲密度
        }
        int intimacy = medalObject.i("intimacy");
        if (intimacy >= giftIntimacy) // 没有升级
        {
            if (debugPrint)
                localNotify("[勋章升级：未升级]");
            return ;
        }
        LiveDanmaku ld = danmaku;
        int level = medalObject.i("level");
        if (debugPrint)
        {
            localNotify("[勋章升级：" + snum(level) + "级]");
        }
        if (ld.getAnchorRoomid() != roomId && (!shortId.isEmpty() && ld.getAnchorRoomid() != shortId)) // 没有戴本房间的牌子
        {
            if (debugPrint)
                localNotify("[勋章升级：非本房间 " + ld.getAnchorRoomid() + "]");
            ld.setMedalLevel(level); // 设置为本房间的牌子
        }
        triggerCmdEvent("MEDAL_UPGRADE", ld);
    });
}

void BaseWindow::startSplash()
{
#ifndef Q_OS_ANDROID
    if (!ui->startupAnimationCheck->isChecked())
        return ;
    RoundedAnimationLabel* label = new RoundedAnimationLabel(this);
    QMovie* movie = new QMovie(":/icons/star_gif");
    movie->setBackgroundColor(Qt::white);
    label->setMovie(movie);
    label->setStyleSheet("background-color: white;");
    label->setGeometry(this->rect());
    label->setAlignment(Qt::AlignCenter);
    int minSize = qMin(label->width(), label->height());
    movie->setScaledSize(QSize(minSize, minSize));
    int lastFrame = movie->frameCount()-1;
    connect(movie, &QMovie::frameChanged, label, [=](int frameNumber){
        if (frameNumber < lastFrame)
            return ;

        movie->stop();
        QPixmap pixmap(label->size());
        label->render(&pixmap);

        // 开始隐藏
        label->startHide();
        movie->deleteLater();
    });
    label->show();
    movie->start();
#endif
}

void BaseWindow::saveGameNumbers(int channel)
{
    auto list = gameNumberLists[channel];
    QStringList sl;
    foreach (qint64 val, list)
        sl << snum(val);
    heaps->setValue("game_numbers/r" + snum(channel), sl.join(";"));
}

void BaseWindow::restoreGameNumbers()
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (!heaps->contains("game_numbers/r" + snum(i)))
            continue;

        QStringList sl = heaps->value("game_numbers/r" + snum(i)).toString().split(";");
        auto& list = gameNumberLists[i];
        foreach (QString s, sl)
            list << s.toLongLong();
    }
}

void BaseWindow::saveGameTexts(int channel)
{
    auto list = gameTextLists[channel];
    heaps->setValue("game_texts/r" + snum(channel), list.join(MAGICAL_SPLIT_CHAR));
}

void BaseWindow::restoreGameTexts()
{
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (!heaps->contains("game_texts/r" + snum(i)))
            continue;

        QStringList sl = heaps->value("game_texts/r" + snum(i)).toString().split(MAGICAL_SPLIT_CHAR);
        gameTextLists[i] = sl;
    }
}

void BaseWindow::setUrlCookie(const QString &url, QNetworkRequest *request)
{
    if (url.contains("bilibili.com") && !browserCookie.isEmpty())
        request->setHeader(QNetworkRequest::CookieHeader, userCookies);
}

QString BaseWindow::GetFileVertion(QString fullName)
{
#if defined(Q_OS_WIN)
    DWORD dwLen = 0;
    char* lpData=NULL;
    LPCWSTR  str_path;
    str_path=fullName.toStdWString().c_str();
    BOOL bSuccess = FALSE;
    QString fileInfomation;
    DWORD vHandle=0;
    //获得文件基础信息
    //--------------------------------------------------------
    dwLen = GetFileVersionInfoSize( str_path, &vHandle);
    if (0 == dwLen)
    {
        qDebug()<<"获取版本字节信息失败!";
        return"";
    }

    lpData =(char*)malloc(dwLen+1);
    if (NULL == lpData)
    {
        qDebug()<<"分配内存失败";
        return "";
    }
    bSuccess = GetFileVersionInfo( fullName.toStdWString().c_str(),0, dwLen+1, lpData);
    if (!bSuccess)
    {
        qDebug()<<"获取文件版本信息错误!";

        return"";
    }
    LPVOID lpBuffer = NULL;
    UINT uLen = 0;

    //获得语言和代码页(language and code page),规定，套用即可
    //---------------------------------------------------
    bSuccess = VerQueryValue( lpData,
                              (TEXT("\\VarFileInfo\\Translation")),
                              &lpBuffer,
                              &uLen);
    QString strTranslation,str1,str2;
    unsigned short int *p =(unsigned short int *)lpBuffer;
    str1.setNum(*p,16);
    str1="000"+ str1;
    strTranslation+= str1.mid(str1.size()-4,4);
    str2.setNum(*(++p),16);
    str2="000"+ str2;
    strTranslation+= str2.mid(str2.size()-4,4);

    QString str_value;
    QString code;
    //以上步骤需按序进行，以下步骤可根据需要增删或者调整

    //获得产品版本信息：ProductVersion
    code ="\\StringFileInfo\\"+ strTranslation +"\\ProductVersion";
    bSuccess = VerQueryValue(lpData,
                             (code.toStdWString().c_str()),
                             &lpBuffer,
                             &uLen);
    if (!bSuccess)
    {
        qDebug()<<"获取产品版本信息错误!";

    }
    else
    {
        str_value=QString::fromUtf16((const unsigned short int *)lpBuffer)+"\n";
    }
    return str_value;
#else
    return "";
#endif
}

/**
 * 升级版本所所需要修改的数据
 */
void BaseWindow::upgradeVersionToLastest(QString oldVersion)
{
    if (oldVersion.startsWith("v") || oldVersion.startsWith("V"))
        oldVersion.replace(0, 1, "");
    QStringList versions = {
        "3.6.3",
        appVersion // 最后一个一定是最新版本
    };
    int index = versions.lastIndexOf(oldVersion);
    if (index < 0)
        index = 0;
    for (int i = index; i < versions.size() - 1; i++)
    {
        QString ver = versions.at(i);
        qDebug() << "从旧版升级：" << ver << " -> " << versions.at(i+1);
        upgradeOneVersionData(ver);
    }
}

void BaseWindow::upgradeOneVersionData(QString beforeVersion)
{
    if (beforeVersion == "3.6.3")
    {
        settings->beginGroup("heaps");
        heaps->beginGroup("heaps");
        auto keys = settings->allKeys();
        for (int i = 0; i < keys.size(); i++)
        {
            QString key = keys.at(i);
            heaps->setValue(key, settings->value(key));
            settings->remove(key);
        }
        heaps->endGroup();
        settings->endGroup();
    }
}

void BaseWindow::generateDefaultCode(QString path)
{
    MyJson json;
    json.insert("welcome", ui->autoWelcomeWordsEdit->toPlainText());
    json.insert("gift", ui->autoThankWordsEdit->toPlainText());
    json.insert("attention", ui->autoAttentionWordsEdit->toPlainText());

    QJsonArray array;
    for (int row = 0; row < ui->taskListWidget->count(); row++)
        array.append(static_cast<TaskWidget*>(ui->taskListWidget->itemWidget(ui->taskListWidget->item(row)))->toJson());
    json.insert("timer_task", array);

    array = QJsonArray();
    for (int row = 0; row < ui->replyListWidget->count(); row++)
        array.append(static_cast<ReplyWidget*>(ui->replyListWidget->itemWidget(ui->replyListWidget->item(row)))->toJson());
    json.insert("auto_reply", array);

    array = QJsonArray();
    for (int row = 0; row < ui->eventListWidget->count(); row++)
        array.append(static_cast<EventWidget*>(ui->eventListWidget->itemWidget(ui->eventListWidget->item(row)))->toJson());
    json.insert("event_action", array);

    json.insert("block_keys", ui->autoBlockNewbieKeysEdit->toPlainText());
    json.insert("block_notify", ui->autoBlockNewbieNotifyWordsEdit->toPlainText());
    json.insert("block_tip", ui->promptBlockNewbieKeysEdit->toPlainText());

    if (path.isEmpty())
        path = QApplication::applicationDirPath() + "/default_code.json";
    writeTextFile(path, QString::fromUtf8(json.toBa()));
}

void BaseWindow::readDefaultCode(QString path)
{
    if (path.isEmpty())
    {
        path = QApplication::applicationDirPath() + "/default_code.json";
        if (!QFileInfo(path).exists())
            path = ":/documents/default_code";
    }

    QString text = readTextFileIfExist(path);
    MyJson json(text.toUtf8());
    if (json.contains("welcome"))
    {
        ui->autoWelcomeWordsEdit->setPlainText(json.s("welcome"));
    }
    if (json.contains("gift"))
    {
        ui->autoThankWordsEdit->setPlainText(json.s("gift"));
    }
    if (json.contains("attention"))
    {
        ui->autoAttentionWordsEdit->setPlainText(json.s("attention"));
    }
    if (json.contains("timer_task"))
    {
        json.each("timer_task", [=](MyJson obj){
            addTimerTask(obj);
        });
        saveTaskList();
    }
    if (json.contains("auto_reply"))
    {
        json.each("auto_reply", [=](MyJson obj){
            addAutoReply(obj);
        });
        saveReplyList();
    }
    if (json.contains("event_action"))
    {
        json.each("event_action", [=](MyJson obj){
            addEventAction(obj);
        });
        saveEventList();
    }
    if (json.contains("block_keys"))
    {
        ui->autoBlockNewbieKeysEdit->setPlainText(json.s("block_keys"));
    }
    if (json.contains("block_notify"))
    {
        ui->autoBlockNewbieNotifyWordsEdit->setPlainText(json.s("block_notify"));
    }
    if (json.contains("block_tip"))
    {
        ui->promptBlockNewbieKeysEdit->setPlainText(json.s("block_tip"));
    }
}

void BaseWindow::on_actionMany_Robots_triggered()
{
    if (!hostList.size()) // 未连接
        return ;

    HostInfo hostServer = hostList.at(0);
    QString host = QString("wss://%1:%2/sub").arg(hostServer.host).arg(hostServer.wss_port);

    QSslConfiguration config = this->socket->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    config.setProtocol(QSsl::TlsV1SslV3);

    for (int i = 0; i < 1000; i++)
    {
        QWebSocket* socket = new QWebSocket();
        connect(socket, &QWebSocket::connected, this, [=]{
            qDebug() << "rSocket" << i << "connected";
            // 5秒内发送认证包
            sendVeriPacket(socket, pkRoomId, pkToken);
        });
        connect(socket, &QWebSocket::disconnected, this, [=]{
            robots_sockets.removeOne(socket);
            socket->deleteLater();
        });
        socket->setSslConfiguration(config);
        socket->open(host);
        robots_sockets.append(socket);
    }
}

void BaseWindow::on_judgeRobotCheck_clicked()
{
    judgeRobot = (judgeRobot + 1) % 3;
    if (judgeRobot == 0)
        ui->judgeRobotCheck->setCheckState(Qt::Unchecked);
    else if (judgeRobot == 1)
        ui->judgeRobotCheck->setCheckState(Qt::PartiallyChecked);
    else if (judgeRobot == 2)
        ui->judgeRobotCheck->setCheckState(Qt::Checked);
    ui->judgeRobotCheck->setText(judgeRobot == 1 ? "机器人判断(仅关注)" : "机器人判断");

    settings->setValue("danmaku/judgeRobot", judgeRobot);
}

void BaseWindow::on_actionAdd_Room_To_List_triggered()
{
    if (roomId.isEmpty())
        return ;

    QStringList list = settings->value("custom/rooms", "").toString().split(";", QString::SkipEmptyParts);
    for (int i = 0; i < list.size(); i++)
    {
        QStringList texts = list.at(i).split(",", QString::SkipEmptyParts);
        if (texts.size() < 1)
            continue ;
        QString id = texts.first();
        QString name = texts.size() >= 2 ? texts.at(1) : id;
        if (id == roomId) // 找到这个
        {
            ui->menu_3->removeAction(ui->menu_3->actions().at(ui->menu_3->actions().size() - (list.size() - i)));
            list.removeAt(i);
            settings->setValue("custom/rooms", list.join(";"));
            return ;
        }
    }

    QString id = roomId; // 不能用成员变量，否则无效（lambda中值会变，自己试试就知道了）
    QString name = upName;

    list.append(id + "," + name.replace(";","").replace(",",""));
    settings->setValue("custom/rooms", list.join(";"));

    QAction* action = new QAction(name, this);
    ui->menu_3->addAction(action);
    connect(action, &QAction::triggered, this, [=]{
        ui->roomIdEdit->setText(id);
        on_roomIdEdit_editingFinished();
    });
}

void BaseWindow::on_recordCheck_clicked()
{
    bool check = ui->recordCheck->isChecked();
    settings->setValue("danmaku/record", check);

    if (check)
    {
        if (!roomId.isEmpty() && isLiving())
            startLiveRecord();
    }
    else
        finishLiveRecord();
}

void BaseWindow::on_recordSplitSpin_valueChanged(int arg1)
{
    settings->setValue("danmaku/recordSplit", arg1);
    if (recordTimer)
        recordTimer->setInterval(arg1 * 60000);
}

void BaseWindow::on_sendWelcomeTextCheck_clicked()
{
    settings->setValue("danmaku/sendWelcomeText", ui->sendWelcomeTextCheck->isChecked());
}

void BaseWindow::on_sendWelcomeVoiceCheck_clicked()
{
    settings->setValue("danmaku/sendWelcomeVoice", ui->sendWelcomeVoiceCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->sendWelcomeVoiceCheck->isChecked())
        initTTS();
#endif
}

void BaseWindow::on_sendGiftTextCheck_clicked()
{
    settings->setValue("danmaku/sendGiftText", ui->sendGiftTextCheck->isChecked());
}

void BaseWindow::on_sendGiftVoiceCheck_clicked()
{
    settings->setValue("danmaku/sendGiftVoice", ui->sendGiftVoiceCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->sendGiftVoiceCheck->isChecked())
        initTTS();
#endif
}

void BaseWindow::on_sendAttentionTextCheck_clicked()
{
    settings->setValue("danmaku/sendAttentionText", ui->sendAttentionTextCheck->isChecked());
}

void BaseWindow::on_sendAttentionVoiceCheck_clicked()
{
    settings->setValue("danmaku/sendAttentionVoice", ui->sendAttentionVoiceCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->sendAttentionVoiceCheck->isChecked())
        initTTS();
#endif
}

void BaseWindow::on_enableScreenDanmakuCheck_clicked()
{
    settings->setValue("screendanmaku/enableDanmaku", ui->enableScreenDanmakuCheck->isChecked());
    ui->enableScreenMsgCheck->setEnabled(ui->enableScreenDanmakuCheck->isChecked());
}

void BaseWindow::on_enableScreenMsgCheck_clicked()
{
    settings->setValue("screendanmaku/enableMsg", ui->enableScreenMsgCheck->isChecked());
}

void BaseWindow::on_screenDanmakuLeftSpin_valueChanged(int arg1)
{
    settings->setValue("screendanmaku/left", arg1);
}

void BaseWindow::on_screenDanmakuRightSpin_valueChanged(int arg1)
{
    settings->setValue("screendanmaku/right", arg1);
}

void BaseWindow::on_screenDanmakuTopSpin_valueChanged(int arg1)
{
    settings->setValue("screendanmaku/top", arg1);
}

void BaseWindow::on_screenDanmakuBottomSpin_valueChanged(int arg1)
{
    settings->setValue("screendanmaku/bottom", arg1);
}

void BaseWindow::on_screenDanmakuSpeedSpin_valueChanged(int arg1)
{
    settings->setValue("screendanmaku/speed", arg1);
}

void BaseWindow::on_screenDanmakuFontButton_clicked()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok,this);
    if (!ok)
        return ;
    this->screenDanmakuFont = font;
    this->setFont(font);
    settings->setValue("screendanmaku/font", screenDanmakuFont.toString());
}

void BaseWindow::on_screenDanmakuColorButton_clicked()
{
    QColor c = QColorDialog::getColor(screenDanmakuColor, this, "选择昵称颜色", QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return ;
    if (c != screenDanmakuColor)
    {
        settings->setValue("screendanmaku/color", screenDanmakuColor = c);
    }
}

void BaseWindow::on_autoSpeekDanmakuCheck_clicked()
{
    settings->setValue("danmaku/autoSpeek", ui->autoSpeekDanmakuCheck->isChecked());
#if defined(ENABLE_TEXTTOSPEECH)
    if (!tts && ui->autoSpeekDanmakuCheck->isChecked())
        initTTS();
#endif
}

void BaseWindow::on_diangeFormatEdit_textEdited(const QString &text)
{
    diangeFormatString = text;
    settings->setValue("danmaku/diangeFormat", diangeFormatString);
}

void BaseWindow::on_diangeNeedMedalCheck_clicked()
{
    settings->setValue("danmaku/diangeNeedMedal", ui->diangeNeedMedalCheck->isChecked());
}

void BaseWindow::on_showOrderPlayerButton_clicked()
{
    on_actionShow_Order_Player_Window_triggered();
}

void BaseWindow::on_diangeShuaCheck_clicked()
{
    settings->setValue("danmaku/diangeShua", ui->diangeShuaCheck->isChecked());
}

void BaseWindow::on_pkMelonValButton_clicked()
{
    bool ok = false;
    int val = QInputDialog::getInt(this, "乱斗值比例", "1乱斗值等于多少金瓜子？10或100？", goldTransPk, 1, 100, 1, &ok);
    if (!ok)
        return ;
    goldTransPk = val;
    settings->setValue("pk/goldTransPk", goldTransPk);
}

void BaseWindow::slotPkEnding()
{
    qDebug() << "大乱斗结束前情况：" << myVotes << matchVotes
             << QDateTime::currentSecsSinceEpoch() << pkEndTime;

    pkEnding = true;
    pkVoting = 0;
    int maxGold = getPkMaxGold(qMax(myVotes, matchVotes));

    // 几个吃瓜就能解决的……
    if (ui->pkAutoMelonCheck->isChecked()
            && myVotes <= matchVotes && myVotes + maxGold/goldTransPk > matchVotes
            && !toutaBlankList.contains(pkRoomId) && !magicalRooms.contains(pkRoomId))
    {
        // 调用送礼
        int melon = 100 / goldTransPk; // 单个吃瓜有多少乱斗值
        int num = static_cast<int>((matchVotes-myVotes+melon)/melon);
        sendGift(20004, num);
        localNotify("[偷塔] " + snum(myVotes) + ":" + snum(matchVotes) + "，赠送 " + snum(num) + " 个吃瓜");
        pkVoting += melon * num; // 增加吃瓜的votes，抵消反偷塔机制中的网络延迟
        qDebug() << "大乱斗赠送" << num << "个吃瓜：" << myVotes << "vs" << matchVotes;
        toutaCount++;
        chiguaCount += num;
        saveTouta();
    }
    else
    {
        QString text = QString("大乱斗尾声：%1 vs %2")
                .arg(myVotes).arg(matchVotes);
        if (!pkRoomId.isEmpty())
        {
            int totalCount = danmakuCounts->value("pk/" + pkRoomId, 0).toInt() - 1;
            int toutaCount = danmakuCounts->value("touta/" + pkRoomId, 0).toInt();
            if (totalCount > 1) // 开始的时候就已经+1了，上面已经-1
                text += QString("  偷塔概率:%1/%2")
                                .arg(toutaCount).arg(totalCount);
        }

        localNotify(text);
    }

    triggerCmdEvent("PK_ENDING", LiveDanmaku());
}

/**
 * 机器人开始工作
 * 开播时连接/连接后开播
 * 不包括视频大乱斗的临时开启
 */
void BaseWindow::slotStartWork()
{
    // 初始化开播数据
    liveTimestamp = QDateTime::currentMSecsSinceEpoch();

    // 自动更换勋章
    if (ui->autoSwitchMedalCheck->isChecked())
    {
        switchMedalTo(roomId.toLongLong());
    }

    ui->actionShow_Live_Video->setEnabled(true);

    // 赠送过期礼物
    if (ui->sendExpireGiftCheck->isChecked())
    {
        sendExpireGift();
    }

    // 挂小心心
    if (ui->acquireHeartCheck->isChecked() && isLiving())
        sendXliveHeartBeatE();

    // 设置直播状态
    QPixmap face = getLivingPixmap(upFace);
    setWindowIcon(face);
    tray->setIcon(face);

    // 开启弹幕保存（但是之前没有开启，怕有bug）
    if (ui->saveDanmakuToFileCheck->isChecked() && !danmuLogFile)
        startSaveDanmakuToFile();

    // 同步所有的使用房间，避免使用神奇弹幕的偷塔误杀
    QString useRoom = roomId;
#ifdef QT_NO_DEBUG
    QTimer::singleShot(300000, [=]{
#else
    QTimer::singleShot(20000, [=]{
#endif
        if (roomId.isEmpty() || useRoom != roomId || !isLiving()) // 使用一段时间后才算真正用上
            return ;
        syncMagicalRooms();
    });

    // 本次直播数据
    liveAllGifts.clear();

    // 获取舰长
    updateExistGuards(0);

    triggerCmdEvent("START_WORK", LiveDanmaku());
}

void BaseWindow::on_autoSwitchMedalCheck_clicked()
{
    settings->setValue("danmaku/autoSwitchMedal", ui->autoSwitchMedalCheck->isChecked());
    if (!roomId.isEmpty() && isLiving())
    {
        switchMedalTo(roomId.toLongLong());
    }
}

void BaseWindow::on_sendAutoOnlyLiveCheck_clicked()
{
    settings->setValue("danmaku/sendAutoOnlyLive", ui->sendAutoOnlyLiveCheck->isChecked());
}

void BaseWindow::on_autoDoSignCheck_clicked()
{
    settings->setValue("danmaku/autoDoSign", ui->autoDoSignCheck->isChecked());
}

void BaseWindow::on_actionRoom_Status_triggered()
{
    RoomStatusDialog* rsd = new RoomStatusDialog(settings, dataPath, nullptr);
    rsd->show();
}

void BaseWindow::on_autoLOTCheck_clicked()
{
    settings->setValue("danmaku/autoLOT", ui->autoLOTCheck->isChecked());
}

void BaseWindow::on_blockNotOnlyNewbieCheck_clicked()
{
    bool enable = ui->blockNotOnlyNewbieCheck->isChecked();
    settings->setValue("block/blockNotOnlyNewbieCheck", enable);
}

void BaseWindow::on_autoBlockTimeSpin_editingFinished()
{
    settings->setValue("block/autoTime", ui->autoBlockTimeSpin->value());
}

/**
 * 所有事件的槽都在这里触发
 */
void BaseWindow::triggerCmdEvent(QString cmd, LiveDanmaku danmaku)
{
    qDebug() << "触发事件：" << cmd;
    emit signalCmdEvent(cmd, danmaku);

    sendDanmakuToSockets(cmd, danmaku);
}

void BaseWindow::on_voiceLocalRadio_toggled(bool checked)
{
    if (checked)
    {
        voicePlatform = VoiceLocal;
        settings->setValue("voice/platform", voicePlatform);
        ui->voiceStack->setCurrentIndex(voicePlatform);
        ui->voiceNameEdit->setText(settings->value("voice/localName").toString());
    }
}

void BaseWindow::on_voiceXfyRadio_toggled(bool checked)
{
    if (checked)
    {
        voicePlatform = VoiceXfy;
        settings->setValue("voice/platform", voicePlatform);
        ui->voiceStack->setCurrentIndex(voicePlatform);
        ui->voiceNameEdit->setText(settings->value("xfytts/name", "xiaoyan").toString());
    }
}

void BaseWindow::on_voiceCustomRadio_toggled(bool checked)
{
    if (checked)
    {
        voicePlatform = VoiceCustom;
        settings->setValue("voice/platform", voicePlatform);
        ui->voiceStack->setCurrentIndex(voicePlatform);
        ui->voiceNameEdit->setText(settings->value("voice/customName").toString());
    }
}

void BaseWindow::on_voiceNameEdit_editingFinished()
{
    voiceName = ui->voiceNameEdit->text();
    switch (voicePlatform) {
    case VoiceLocal:
        settings->setValue("voice/localName", voiceName);
        break;
    case VoiceXfy:
        settings->setValue("xfytts/name", voiceName);
        if (xfyTTS)
        {
            xfyTTS->setName(voiceName);
        }
        break;
    case VoiceCustom:
        settings->setValue("voice/customName", voiceName);
        break;
    }
}

void BaseWindow::on_voiceNameSelectButton_clicked()
{
    if (voicePlatform == VoiceXfy)
    {
        QStringList names{"讯飞小燕<xiaoyan>",
                         "讯飞许久<aisjiuxu>",
                         "讯飞小萍<aisxping>",
                         "讯飞小婧<aisjinger>",
                         "讯飞许小宝<aisbabyxu>"};
        QStringListModel* model = new QStringListModel(names);
        QListView* view = new QListView(nullptr);
        view->setModel(model);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        connect(view, &QListView::activated, this, [=](const QModelIndex &index){
            if (!index.isValid())
                return ;
            int row = index.row();
            QString text = names.at(row);
            QRegularExpression re("<(.+)>");
            QRegularExpressionMatch match;
            if (text.indexOf(re, 0, &match) > -1)
                text = match.capturedTexts().at(1);
            this->ui->voiceNameEdit->setText(text);
            if (xfyTTS)
                xfyTTS->setName(text);
            view->deleteLater();
            model->deleteLater();
        });
        view->show();
    }
}

void BaseWindow::on_voicePitchSlider_valueChanged(int value)
{
    settings->setValue("voice/pitch", voicePitch = value);
    ui->voicePitchLabel->setText("音调" + snum(value));

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (tts)
        {
            tts->setPitch((voicePitch - 50) / 50.0);
        }
#endif
        break;
    case VoiceXfy:
        if (xfyTTS)
        {
            xfyTTS->setPitch(voicePitch);
        }
        break;
    case VoiceCustom:
        break;
    }
}

void BaseWindow::on_voiceSpeedSlider_valueChanged(int value)
{
    settings->setValue("voice/speed", voiceSpeed = value);
    ui->voiceSpeedLabel->setText("音速" + snum(value));

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (tts)
        {
            tts->setRate((voiceSpeed - 50) / 50.0);
        }
#endif
        break;
    case VoiceXfy:
        if (xfyTTS)
        {
            xfyTTS->setSpeed(voiceSpeed);
        }
        break;
    case VoiceCustom:
        break;
    }
}

void BaseWindow::on_voiceVolumeSlider_valueChanged(int value)
{
    settings->setValue("voice/volume", voiceVolume = value);
    ui->voiceVolumeLabel->setText("音量" + snum(value));

    switch (voicePlatform) {
    case VoiceLocal:
#if defined(ENABLE_TEXTTOSPEECH)
        if (tts)
        {
            tts->setVolume((voiceVolume) / 100.0);
        }
#endif
        break;
    case VoiceXfy:
        if (xfyTTS)
        {
            xfyTTS->setVolume(voiceVolume);
        }
        break;
    case VoiceCustom:
        break;
    }
}

void BaseWindow::on_voicePreviewButton_clicked()
{
    speakText("这是一个语音合成示例");
}

void BaseWindow::on_voiceLocalRadio_clicked()
{
#if defined(ENABLE_TEXTTOSPEECH)
    QTimer::singleShot(100, [=]{
        if (!tts)
        {
            initTTS();
        }
        else
        {
            tts->setRate( (voiceSpeed = settings->value("voice/speed", 50).toInt() - 50) / 50.0 );
            tts->setPitch( (voicePitch = settings->value("voice/pitch", 50).toInt() - 50) / 50.0 );
            tts->setVolume( (voiceVolume = settings->value("voice/volume", 50).toInt()) / 100.0 );
        }
    });
#endif
}

void BaseWindow::on_voiceXfyRadio_clicked()
{
    QTimer::singleShot(100, [=]{
        if (!xfyTTS)
        {
            initTTS();
        }
        else
        {
            xfyTTS->setName( voiceName = settings->value("xfytts/name", "xiaoyan").toString() );
            xfyTTS->setPitch( voicePitch = settings->value("voice/pitch", 50).toInt() );
            xfyTTS->setSpeed( voiceSpeed = settings->value("voice/speed", 50).toInt() );
            xfyTTS->setVolume( voiceSpeed = settings->value("voice/speed", 50).toInt() );
        }
    });
}

void BaseWindow::on_voiceCustomRadio_clicked()
{
    QTimer::singleShot(100, [=]{
        initTTS();
    });
}

void BaseWindow::on_label_10_linkActivated(const QString &link)
{
    QDesktopServices::openUrl(link);
}

void BaseWindow::on_xfyAppIdEdit_textEdited(const QString &text)
{
    settings->setValue("xfytts/appid", text);
    if (xfyTTS)
    {
        xfyTTS->setAppId(text);
    }
}

void BaseWindow::on_xfyApiSecretEdit_textEdited(const QString &text)
{
    settings->setValue("xfytts/apisecret", text);
    if (xfyTTS)
    {
        xfyTTS->setApiSecret(text);
    }
}

void BaseWindow::on_xfyApiKeyEdit_textEdited(const QString &text)
{
    settings->setValue("xfytts/apikey", text);
    if (xfyTTS)
    {
        xfyTTS->setApiKey(text);
    }
}

void BaseWindow::on_voiceCustomUrlEdit_editingFinished()
{
    settings->setValue("voice/customUrl", ui->voiceCustomUrlEdit->text());
}


void BaseWindow::on_eternalBlockListButton_clicked()
{
    EternalBlockDialog* dialog = new EternalBlockDialog(&eternalBlockUsers, this);
    connect(dialog, SIGNAL(signalCancelEternalBlock(qint64)), this, SLOT(cancelEternalBlockUser(qint64)));
    connect(dialog, SIGNAL(signalCancelBlock(qint64)), this, SLOT(cancelEternalBlockUserAndUnblock(qint64)));
    dialog->exec();
}

void BaseWindow::on_AIReplyMsgCheck_clicked()
{
    Qt::CheckState state = ui->AIReplyMsgCheck->checkState();
    settings->setValue("danmaku/aiReplyMsg", state);
    if (state != Qt::PartiallyChecked)
        ui->AIReplyMsgCheck->setText("回复弹幕");
    else
        ui->AIReplyMsgCheck->setText("回复弹幕(仅单条)");
}

void BaseWindow::slotAIReplyed(QString reply, qint64 uid)
{
    if (ui->AIReplyMsgCheck->checkState() != Qt::Unchecked)
    {
        // 机器人自己的不回复（不然自己和自己打起来了）
        if (snum(uid) == cookieUid)
            return ;

        // AI回复长度上限，以及过滤
        if (ui->AIReplyMsgCheck->checkState() == Qt::PartiallyChecked
                && reply.length() > danmuLongest)
            return ;

        // 自动断句
        QStringList sl;
        int len = reply.length();
        const int maxOne = danmuLongest;
        int count = (len + maxOne - 1) / maxOne;
        for (int i = 0; i < count; i++)
        {
            sl << reply.mid(i * maxOne, maxOne);
        }
        sendAutoMsg(sl.join("\\n"));
    }
}

void BaseWindow::on_danmuLongestSpin_editingFinished()
{
    danmuLongest = ui->danmuLongestSpin->value();
    settings->setValue("danmaku/danmuLongest", danmuLongest);
}


void BaseWindow::on_startupAnimationCheck_clicked()
{
    settings->setValue("mainwindow/splash", ui->startupAnimationCheck->isChecked());
}

void BaseWindow::on_serverCheck_clicked()
{
    bool enabled = ui->serverCheck->isChecked();
    settings->setValue("server/enabled", enabled);
    if (enabled)
        openServer();
    else
        closeServer();
}

void BaseWindow::on_serverPortSpin_editingFinished()
{
    settings->setValue("server/port", ui->serverPortSpin->value());
#if defined(ENABLE_HTTP_SERVER)
    if (server)
    {
        closeServer();
        QTimer::singleShot(1000, [=]{
            openServer();
        });
    }
#endif
}

void BaseWindow::on_autoPauseOuterMusicCheck_clicked()
{
    settings->setValue("danmaku/autoPauseOuterMusic", ui->autoPauseOuterMusicCheck->isChecked());
}

void BaseWindow::on_outerMusicKeyEdit_textEdited(const QString &arg1)
{
    settings->setValue("danmaku/outerMusicPauseKey", arg1);
}

void BaseWindow::on_acquireHeartCheck_clicked()
{
    settings->setValue("danmaku/acquireHeart", ui->acquireHeartCheck->isChecked());

    if (ui->acquireHeartCheck->isChecked())
    {
        if (isLiving())
            sendXliveHeartBeatE();
    }
    else
    {
        if (xliveHeartBeatTimer)
            xliveHeartBeatTimer->stop();
    }
}

void BaseWindow::on_sendExpireGiftCheck_clicked()
{
    bool enable = ui->sendExpireGiftCheck->isChecked();
    settings->setValue("danmaku/sendExpireGift", enable);

    if (enable)
    {
        sendExpireGift();
    }
}

void BaseWindow::on_actionPicture_Browser_triggered()
{
    if (!pictureBrowser)
    {
        pictureBrowser = new PictureBrowser(settings, nullptr);
    }
    pictureBrowser->show();
    pictureBrowser->readDirectory(dataPath + "captures");
}

void BaseWindow::on_orderSongsToFileCheck_clicked()
{
    settings->setValue("danmaku/orderSongsToFile", ui->orderSongsToFileCheck->isChecked());

    if (musicWindow && ui->orderSongsToFileCheck->isChecked())
    {
        saveOrderSongs(musicWindow->getOrderSongs());
    }
}

void BaseWindow::on_orderSongsToFileFormatEdit_textEdited(const QString &arg1)
{
    settings->setValue("danmaku/orderSongsToFileFormat", arg1);

    if (musicWindow && ui->orderSongsToFileCheck->isChecked())
    {
        saveOrderSongs(musicWindow->getOrderSongs());
    }
}

void BaseWindow::on_orderSongsToFileMaxSpin_editingFinished()
{
    settings->setValue("danmaku/orderSongsToFileMax", ui->orderSongsToFileMaxSpin->value());

    if (musicWindow && ui->orderSongsToFileCheck->isChecked())
    {
        saveOrderSongs(musicWindow->getOrderSongs());
    }
}

void BaseWindow::on_actionCatch_You_Online_triggered()
{
    CatchYouWidget* cyw = new CatchYouWidget(settings, dataPath, nullptr);
    cyw->show();
    // cyw->catchUser(upUid);
    cyw->setDefaultUser(upUid);
}

void BaseWindow::on_pkBlankButton_clicked()
{
    bool ok = false;
    QString text = QInputDialog::getText(this, "自动偷塔黑名单", "设置不启用自动偷塔的房间号列表\n多个房间用任意非数字符号分隔",
                                         QLineEdit::Normal, toutaBlankList.join(";"), &ok);
    if (!ok)
        return ;
    toutaBlankList = text.split(QRegExp("[^\\d]+"), QString::SkipEmptyParts);
    settings->setValue("pk/blankList", toutaBlankList.join(";"));
}

void BaseWindow::on_actionUpdate_New_Version_triggered()
{
    if (!appDownloadUrl.isEmpty())
    {
        QDesktopServices::openUrl(QUrl(appDownloadUrl));
    }
    else
    {
        syncMagicalRooms();
    }
}

void BaseWindow::on_startOnRebootCheck_clicked()
{
    bool enable = ui->startOnRebootCheck->isChecked();
    settings->setValue("runtime/startOnReboot", enable);

    QString appName = QApplication::applicationName();
    QString appPath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    QSettings *reg=new QSettings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QString val = reg->value(appName).toString();// 如果此键不存在，则返回的是空字符串
    if (enable)
        reg->setValue(appName, appPath);// 如果移除的话
    else
        reg->remove(appName);
    reg->deleteLater();
}

void BaseWindow::on_domainEdit_editingFinished()
{
    serverDomain = ui->domainEdit->text().trimmed();
    if (serverDomain.isEmpty())
        serverDomain = "localhost";
    settings->setValue("server/domain", serverDomain);
}

void BaseWindow::on_AIReplyIdButton_clicked()
{
    QString replyAppId = settings->value("reply/APPID", "").toString();
    bool ok = false;
    QString text = QInputDialog::getText(this, "AI回复的APPID", "可在 https://ai.qq.com/console 申请\n自定义机器人画像，包括名字、性格等", QLineEdit::Normal, replyAppId, &ok);
    if (!ok)
        return ;
    settings->setValue("reply/APPID", text);
    if (danmakuWindow)
        danmakuWindow->readReplyKey();
}

void BaseWindow::on_AIReplyKeyButton_clicked()
{
    QString replyAppKey = settings->value("reply/APPKEY", "").toString();
    bool ok = false;
    QString text = QInputDialog::getText(this, "AI回复的APPKEY", "可在 https://ai.qq.com/console 申请\n自定义机器人画像，包括名字、性格等", QLineEdit::Normal, replyAppKey, &ok);
    if (!ok)
        return ;
    settings->setValue("reply/APPKEY", text);
    if (danmakuWindow)
        danmakuWindow->readReplyKey();
}

void BaseWindow::prepareQuit()
{
    releaseLiveData();
    qApp->quit();
}

void BaseWindow::on_giftComboSendCheck_clicked()
{
    settings->setValue("danmaku/giftComboSend", ui->giftComboSendCheck->isChecked());
}

void BaseWindow::on_giftComboDelaySpin_editingFinished()
{
    settings->setValue("danmaku/giftComboDelay", ui->giftComboDelaySpin->value());
}

void BaseWindow::on_retryFailedDanmuCheck_clicked()
{
    settings->setValue("danmaku/retryFailedDanmu", ui->retryFailedDanmuCheck->isChecked());
}

void BaseWindow::on_songLyricsToFileCheck_clicked()
{
    settings->setValue("danmaku/songLyricsToFile", ui->songLyricsToFileCheck->isChecked());

    if (musicWindow && ui->songLyricsToFileCheck->isChecked())
    {
        saveSongLyrics();
    }
    if (musicWindow && sendLyricListToSockets)
    {
        sendLyricList();
    }
}

void BaseWindow::on_songLyricsToFileMaxSpin_editingFinished()
{
    settings->setValue("danmaku/songLyricsToFileMax", ui->songLyricsToFileMaxSpin->value());

    if (musicWindow && ui->songLyricsToFileCheck->isChecked())
    {
        saveSongLyrics();
    }
    if (musicWindow && sendLyricListToSockets)
    {
        sendLyricList();
    }
}

void BaseWindow::on_allowWebControlCheck_clicked()
{
    settings->setValue("server/allowWebControl", ui->allowWebControlCheck->isChecked());
}

void BaseWindow::on_saveEveryGuardCheck_clicked()
{
    settings->setValue("danmaku/saveEveryGuard", ui->saveEveryGuardCheck->isChecked());
}

void BaseWindow::on_saveMonthGuardCheck_clicked()
{
    settings->setValue("danmaku/saveMonthGuard", ui->saveMonthGuardCheck->isChecked());
}

void BaseWindow::on_saveEveryGiftCheck_clicked()
{
    settings->setValue("danmaku/saveEveryGift", ui->saveEveryGiftCheck->isChecked());
}

void BaseWindow::on_exportDailyButton_clicked()
{
    if (roomId.isEmpty())
        return ;

    QString oldPath = settings->value("danmaku/exportPath", "").toString();
    QString path = QFileDialog::getSaveFileName(this, "选择导出位置", oldPath, "Tables (*.csv *.txt)");
    if (path.isEmpty())
        return ;
    settings->setValue("danmaku/exportPath", path);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    QTextStream stream(&file);
    if (!recordFileCodec.isEmpty())
        stream.setCodec(recordFileCodec.toUtf8());

    // 拼接数据
    QString dirPath = dataPath + "live_daily";
    QDir dir(dirPath);
    auto files = dir.entryList(QStringList{roomId + "_*.ini"}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    stream << QString("日期,进入人次,进入人数,弹幕数量,新人弹幕,新增关注,关注总数,总金瓜子,总银瓜子,上船人数,船员总数\n").toUtf8();
    for (int i = 0; i < files.size(); i++)
    {
        QStringList sl;
        QSettings st(dirPath + "/" + files.at(i), QSettings::Format::IniFormat);
        QString day = files.at(i);
        day.replace(roomId + "_", "").replace(".ini", "");
        stream << day << ","
               << st.value("come", 0).toInt() << ","
               << st.value("people_num", 0).toInt() << ","
               << st.value("danmaku", 0).toInt() << ","
               << st.value("newbie_msg", 0).toInt() << ","
               << st.value("new_fans", 0).toInt() << ","
               << st.value("total_fans", 0).toInt() << ","
               << st.value("gift_gold", 0).toInt() << ","
               << st.value("gift_silver", 0).toInt() << ","
               << st.value("guard", 0).toInt() << ","
               << st.value("guard_count", 0).toInt()  << ","
               << st.value("average_popularity", 0).toInt() << ","
               << st.value("max_popularity", 0).toInt()
               << "\n";
    }

    file.close();
}

void BaseWindow::on_closeTransMouseButton_clicked()
{
    if (danmakuWindow)
        danmakuWindow->closeTransMouse();
}

void BaseWindow::on_pkAutoMaxGoldCheck_clicked()
{
    settings->setValue("pk/autoMaxGold", ui->pkAutoMaxGoldCheck->isChecked());
}

void BaseWindow::on_saveRecvCmdsCheck_clicked()
{
    saveRecvCmds = ui->saveRecvCmdsCheck->isChecked();
    settings->setValue("debug/saveRecvCmds", saveRecvCmds);

    if (saveRecvCmds)
    {
        QDir dir;
        dir.mkdir(dataPath+"websocket_cmds");
        QString date = QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss");
        saveCmdsFile = new QFile(dataPath+"websocket_cmds/" + roomId + "_" + date + ".txt");
        saveCmdsFile->open(QIODevice::WriteOnly | QIODevice::Append);
        qDebug() << "开始保存cmds：" << dataPath+"websocket_cmds/" + roomId + "_" + date + ".txt";
    }
    else if (saveCmdsFile)
    {
        qDebug() << "结束保存cmds";
        saveCmdsFile->close();
        saveCmdsFile->deleteLater();
        saveCmdsFile = nullptr;
    }
}

void BaseWindow::on_allowRemoteControlCheck_clicked()
{
    settings->setValue("danmaku/remoteControl", remoteControl = ui->allowRemoteControlCheck->isChecked());
}

void BaseWindow::on_actionJoin_Battle_triggered()
{
    joinBattle(1);
}

void BaseWindow::on_actionQRCode_Login_triggered()
{
    QRCodeLoginDialog* dialog = new QRCodeLoginDialog(this);
    connect(dialog, &QRCodeLoginDialog::logined, this, [=](QString s){
        autoSetCookie(s);
    });
    dialog->exec();
}

void BaseWindow::on_allowAdminControlCheck_clicked()
{
    settings->setValue("danmaku/adminControl", ui->allowAdminControlCheck->isChecked());
}

void BaseWindow::on_actionSponsor_triggered()
{
    EscapeDialog* dialog = new EscapeDialog("友情赞助", "您的支持是开发者为爱发电的最大动力！", "不想付钱", "感谢支持", this);
    dialog->exec();
}

void BaseWindow::on_actionPaste_Code_triggered()
{
    QString clipText = QApplication::clipboard()->text();
    if (clipText.isEmpty())
    {
        QMessageBox::information(this, "粘贴代码片段", "在定时任务、自动回复、事件动作等列表的右键菜单中复制的结果，可用于备份或发送至其余地方");
        return ;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(clipText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::information(this, "粘贴代码片段", "只支持JSON格式的代码配置\n" + error.errorString());
        return ;
    }

    auto pasteFromJson = [=](QJsonObject json) {
        QString anchor_key = json.value("anchor_key").toString();
        ListItemInterface* item = nullptr;
        if (anchor_key == CODE_TIMER_TASK_KEY)
        {
            item = addTimerTask(false, 1800, "");
            ui->tabWidget->setCurrentWidget(ui->tabTimer);
            ui->taskListWidget->scrollToBottom();
            settings->setValue("task/count", ui->taskListWidget->count());
        }
        else if (anchor_key == CODE_AUTO_REPLY_KEY)
        {
            item = addAutoReply(false, "","");
            ui->tabWidget->setCurrentWidget(ui->tabReply);
            ui->replyListWidget->scrollToBottom();
            settings->setValue("reply/count", ui->replyListWidget->count());
        }
        else if (anchor_key == CODE_EVENT_ACTION_KEY)
        {
            item = addEventAction(false, "", "");
            ui->tabWidget->setCurrentWidget(ui->tabEvent);
            ui->eventListWidget->scrollToBottom();
            settings->setValue("event/count", ui->eventListWidget->count());
        }
        else
        {
            qWarning() << "未知格式：" << json;
            return ;
        }

        item->fromJson(json);

        if (anchor_key == CODE_AUTO_REPLY_KEY)
        {
            for (int row = 0; row < ui->replyListWidget->count() - 1; row++)
            {
                auto rowItem = ui->replyListWidget->item(row);
                auto widget = ui->replyListWidget->itemWidget(rowItem);
                if (!widget)
                    continue;
                QSize size(ui->replyListWidget->contentsRect().width() - ui->replyListWidget->verticalScrollBar()->width(), widget->height());
                auto replyWidget = static_cast<ReplyWidget*>(widget);

                auto rw = static_cast<ReplyWidget*>(item);
                if (rw->keyEdit->text() == replyWidget->keyEdit->text()
                        && rw->replyEdit->toPlainText() == replyWidget->replyEdit->toPlainText())
                {
                    rw->check->setChecked(false);
                    rw->keyEdit->setText(rw->keyEdit->text() + "_重复");
                    break;
                }
            }
        }
        else if (anchor_key == CODE_EVENT_ACTION_KEY)
        {
            for (int row = 0; row < ui->eventListWidget->count() - 1; row++)
            {
                auto rowItem = ui->eventListWidget->item(row);
                auto widget = ui->eventListWidget->itemWidget(rowItem);
                if (!widget)
                    continue;
                QSize size(ui->eventListWidget->contentsRect().width() - ui->eventListWidget->verticalScrollBar()->width(), widget->height());
                auto eventWidget = static_cast<EventWidget*>(widget);

                auto rw = static_cast<EventWidget*>(item);
                if (rw->eventEdit->text() == eventWidget->eventEdit->text()
                        && rw->actionEdit->toPlainText() == eventWidget->actionEdit->toPlainText())
                {
                    rw->check->setChecked(false);
                    rw->eventEdit->setText(rw->eventEdit->text() + "_重复");
                    break;
                }
            }
        }
    };

    if (doc.isObject())
    {
        pasteFromJson(doc.object());
    }
    else if (doc.isArray())
    {
        QJsonArray array = doc.array();
        foreach (QJsonValue val, array)
        {
            pasteFromJson(val.toObject());
        }
    }
}

void BaseWindow::on_actionGenerate_Default_Code_triggered()
{
    QString oldPath = settings->value("danmaku/codePath", "").toString();
    QString path = QFileDialog::getSaveFileName(this, "选择导出位置", oldPath, "Json (*.json *.txt)");
    if (path.isEmpty())
        return ;
    settings->setValue("danmaku/codePath", path);

    generateDefaultCode(path);
}

void BaseWindow::on_actionRead_Default_Code_triggered()
{
    QString oldPath = settings->value("danmaku/codePath", "").toString();
    QString path = QFileDialog::getOpenFileName(this, "选择读取位置", oldPath, "Json (*.json *.txt)");
    if (path.isEmpty())
        return ;
    settings->setValue("danmaku/codePath", path);

    readDefaultCode(path);
}

void BaseWindow::on_giftComboTopCheck_clicked()
{
    settings->setValue("danmaku/giftComboTop", ui->giftComboTopCheck->isChecked());
}

void BaseWindow::on_giftComboMergeCheck_clicked()
{
    settings->setValue("danmaku/giftComboMerge", ui->giftComboMergeCheck->isChecked());
}

void BaseWindow::on_listenMedalUpgradeCheck_clicked()
{
    settings->setValue("danmaku/listenMedalUpgrade", ui->listenMedalUpgradeCheck->isChecked());
}

void BaseWindow::on_pushRecvCmdsButton_clicked()
{
    if (!pushCmdsFile) // 开启模拟输入
    {
        // 输入文件
        QString oldPath = settings->value("debug/cmdsPath", "").toString();
        QString path = QFileDialog::getOpenFileName(this, "选择模拟输入的CMDS文件位置", oldPath, "Text (*.txt)");
        if (path.isEmpty())
            return ;
        settings->setValue("debug/cmdsPath", path);

        // 创建文件对象
        pushCmdsFile = new QFile(path);
        if (!pushCmdsFile->open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QMessageBox::critical(this, "模拟CMDS", "读取文件失败");
            return ;
        }

        // 本地模式提示
        if (!ui->actionLocal_Mode->isChecked())
        {
            if (QMessageBox::question(this, "模拟CMDS", "您的[本地模式]未开启，可能会回复奇奇怪怪的内容，是否开启[本地模式]？",
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
                ui->actionLocal_Mode->setChecked(localDebug = true);
        }
        ui->saveRecvCmdsCheck->setChecked(saveRecvCmds = false);

        // 定时器
        if (!pushCmdsTimer)
        {
            pushCmdsTimer = new QTimer(this);
            pushCmdsTimer->setInterval(ui->timerPushCmdSpin->value() * 100);
            connect(pushCmdsTimer, SIGNAL(timeout()), this, SLOT(on_pushNextCmdButton_clicked()));
        }
        pushCmdsTimer->setInterval(ui->timerPushCmdSpin->value() * 100);
        if (ui->timerPushCmdCheck->isChecked())
            pushCmdsTimer->start();

        ui->pushRecvCmdsButton->setText("停止");
        ui->pushNextCmdButton->show();
        ui->timerPushCmdCheck->show();
        ui->timerPushCmdSpin->show();
    }
    else // 关闭模拟输入
    {
        if (ui->actionLocal_Mode->isChecked() && settings->value("debug/localDebug", false).toBool())
            ui->actionLocal_Mode->setChecked(localDebug = false);

        if (settings->value("debug/saveRecvCmds", false).toBool())
            ui->saveRecvCmdsCheck->setChecked(saveRecvCmds = true);

        if (pushCmdsTimer)
            pushCmdsTimer->stop();

        ui->pushRecvCmdsButton->setText("输入CMDS");
        ui->pushNextCmdButton->hide();
        ui->timerPushCmdCheck->hide();
        ui->timerPushCmdSpin->hide();

        pushCmdsFile->close();
        pushCmdsFile->deleteLater();
        pushCmdsFile = nullptr;
    }
}

void BaseWindow::on_pushNextCmdButton_clicked()
{
    if (!pushCmdsFile)
        return ;

    QByteArray line = pushCmdsFile->readLine();
    if (line.isNull())
    {
        localNotify("[模拟输入CMDS结束，已退出]");
        on_pushRecvCmdsButton_clicked();
        return ;
    }

    // 处理下一行
    line.replace("__bmd__n__", "\n").replace("__bmd__r__", "\r");
    slotBinaryMessageReceived(line);
}

void BaseWindow::on_timerPushCmdCheck_clicked()
{
    bool enable = ui->timerPushCmdCheck->isChecked();
    settings->setValue("debug/pushCmdsTimer", enable);

    if (pushCmdsFile && pushCmdsTimer)
    {
        if (enable)
            pushCmdsTimer->start();
        else
            pushCmdsTimer->stop();
    }
}

void BaseWindow::on_timerPushCmdSpin_editingFinished()
{
    int val = ui->timerPushCmdSpin->value();
    settings->setValue("debug/pushCmdsInterval", val);

    if (pushCmdsTimer)
        pushCmdsTimer->setInterval(val * 100);
}

void BaseWindow::on_pkChuanmenCheck_stateChanged(int arg1)
{
    ui->pkMsgSyncCheck->setEnabled(arg1);
}

void BaseWindow::on_actionLast_Candidate_triggered()
{
    QMessageBox::information(this, "最后一次调试的候选弹幕", "-------- 填充变量 --------\n\n" + lastConditionDanmu + "\n\n-------- 随机发送 --------\n\n" + lastCandidateDanmaku);
}

void BaseWindow::on_actionLocal_Mode_triggered()
{
    settings->setValue("debug/localDebug", localDebug = ui->actionLocal_Mode->isChecked());
}

void BaseWindow::on_actionDebug_Mode_triggered()
{
    settings->setValue("debug/debugPrint", debugPrint = ui->actionDebug_Mode->isChecked());
}

void BaseWindow::on_actionGuard_Online_triggered()
{
    GuardOnlineDialog* god = new GuardOnlineDialog(settings, roomId, upUid, this);
    god->show();
}

void BaseWindow::on_actionOfficial_Website_triggered()
{
    QDesktopServices::openUrl(QUrl("http://danmaku.lyixi.com"));
}

void BaseWindow::on_actionAnchor_Case_triggered()
{
    QDesktopServices::openUrl(QUrl("http://anchor.lyixi.com"));
}
