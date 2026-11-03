#include "roomdialog.h"
#include "ui_roomdialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTextCursor>
#include <QTime>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "system_audio_read.h"

RoomDialog::RoomDialog(QWidget *parent) :
    CustomMoveDialog(parent),
    ui(new Ui::RoomDialog),
    m_latencyLabel(new LatencyLabel(this))
{
    ui->setupUi(this);

    setWindowTitle("Audio-Video");
    resize(1280, 800);
    setMinimumSize(960, 640);
    ui->wdg_top->setFixedHeight(52);
    ui->wdg_bottom->setFixedHeight(66);
    ui->horizontalLayout->setContentsMargins(16, 8, 8, 8);
    ui->horizontalLayout_3->setContentsMargins(16, 14, 16, 14);
    ui->horizontalLayout_3->setSpacing(14);
    ui->horizontalLayout_3->setStretch(0, 1);
    ui->horizontalLayout_3->setStretch(1, 0);
    ui->horizontalLayout_2->setContentsMargins(18, 12, 18, 12);
    ui->horizontalLayout_2->setSpacing(10);
    ui->sa_userList->setFixedWidth(264);
    ui->pb_quit->setFixedSize(86, 36);
    ui->cbx_moji->setFixedWidth(108);
    ui->pb_hide->setToolTip("显示或隐藏参会者");
    ui->pb_min->setToolTip("最小化");
    ui->pb_max->setToolTip("最大化");
    ui->pb_close->setToolTip("退出会议");

    ui->horizontalLayout->insertWidget(0, m_latencyLabel);
    m_mediaStatus = new QLabel("视频未连接", this);
    ui->horizontalLayout->insertWidget(1, m_mediaStatus);
    m_deskStatusLabel = new QLabel("共享 未开始", this);
    m_deskStatusLabel->setObjectName("lb_deskStatus");
    m_deskStatusLabel->setMinimumHeight(28);
    m_deskStatusLabel->setToolTip("桌面共享的实际编码状态");
    ui->horizontalLayout->insertWidget(1, m_deskStatusLabel);
    setTopStatusVisible(false);
    connect(m_latencyLabel, &LatencyLabel::clicked, this, [this]() {
        setTopStatusVisible(!m_topStatusVisible);
    });

    auto *viewScaleLabel = new QLabel("查看", this);
    viewScaleLabel->setMinimumHeight(28);
    m_viewScaleCombo = new QComboBox(this);
    m_viewScaleCombo->setMinimumHeight(28);
    m_viewScaleCombo->setFixedWidth(96);
    m_viewScaleCombo->setToolTip("调整大画面的查看倍率；放大后可按住画面拖动");
    m_viewScaleCombo->addItem("适应窗口", 0.0);
    m_viewScaleCombo->addItem("100%", 1.0);
    m_viewScaleCombo->addItem("125%", 1.25);
    m_viewScaleCombo->addItem("150%", 1.5);
    ui->horizontalLayout->insertWidget(2, viewScaleLabel);
    ui->horizontalLayout->insertWidget(3, m_viewScaleCombo);
    connect(m_viewScaleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) {
        ui->wdg_bigShow->slot_setViewScale(m_viewScaleCombo->currentData().toDouble());
    });

    auto *qualityLabel = new QLabel("画质", this);
    qualityLabel->setMinimumHeight(28);
    m_deskResolutionCombo = new QComboBox(this);
    m_deskResolutionCombo->setMinimumHeight(28);
    m_deskResolutionCombo->setFixedWidth(88);
    m_deskResolutionCombo->setToolTip("选择桌面共享的最大输出分辨率");
    m_deskResolutionCombo->addItem("720p", QSize(1280, 720));
    m_deskResolutionCombo->addItem("1080p", QSize(1920, 1080));
    m_deskResolutionCombo->addItem("1440p", QSize(2560, 1440));
    m_deskResolutionCombo->addItem("4K", QSize(3840, 2160));
    m_deskResolutionCombo->setCurrentIndex(2);

    auto *frameRateLabel = new QLabel("FPS", this);
    frameRateLabel->setMinimumHeight(28);
    m_deskFrameRateCombo = new QComboBox(this);
    m_deskFrameRateCombo->setMinimumHeight(28);
    m_deskFrameRateCombo->setFixedWidth(72);
    m_deskFrameRateCombo->setToolTip("选择桌面共享帧率");
    m_deskFrameRateCombo->addItem("15 FPS", 15);
    m_deskFrameRateCombo->addItem("30 FPS", 30);
    m_deskFrameRateCombo->addItem("60 FPS", 60);
    m_deskFrameRateCombo->setCurrentIndex(2);

    auto *screenLabel = new QLabel("屏幕", this);
    screenLabel->setMinimumHeight(28);
    m_deskScreenCombo = new QComboBox(this);
    m_deskScreenCombo->setMinimumHeight(28);
    m_deskScreenCombo->setFixedWidth(148);
    m_deskScreenCombo->setToolTip("选择要共享的物理显示器");
    refreshDesktopScreens();

    m_systemAudioCheck = new QCheckBox("系统声音", this);
    m_systemAudioCheck->setMinimumHeight(28);
    m_systemAudioCheck->setToolTip("把选定播放设备的系统声音与麦克风一起发送；建议使用耳机避免回声");
    m_systemAudioCombo = new QComboBox(this);
    m_systemAudioCombo->setMinimumHeight(28);
    m_systemAudioCombo->setMinimumWidth(140);
    m_systemAudioCombo->setMaximumWidth(190);
    m_systemAudioCombo->setToolTip("选择要采集的播放设备；默认会跟随 Windows 默认输出");
    refreshSystemAudioDevices();
    m_systemAudioCombo->installEventFilter(this);
    setSystemAudioControlsEnabled(false);

    int desktopControlIndex = ui->horizontalLayout_2->indexOf(ui->cb_desk) + 1;
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, qualityLabel);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, m_deskResolutionCombo);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, frameRateLabel);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, m_deskFrameRateCombo);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, screenLabel);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex, m_deskScreenCombo);
    const int audioControlIndex = ui->horizontalLayout_2->indexOf(ui->cb_audio) + 1;
    ui->horizontalLayout_2->insertWidget(audioControlIndex, m_systemAudioCheck);
    ui->horizontalLayout_2->insertWidget(audioControlIndex + 1, m_systemAudioCombo);
    connect(m_systemAudioCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            slot_setSystemAudioError(QString());
        }
        Q_EMIT SIG_systemAudioStateChanged(checked);
    });
    connect(m_systemAudioCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
        Q_EMIT SIG_systemAudioDeviceChanged(m_systemAudioCombo->currentData().toString());
    });
    connect(m_deskResolutionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emitDesktopShareSettings(); });
    connect(m_deskFrameRateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emitDesktopShareSettings(); });
    connect(m_deskScreenCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emitDesktopShareScreen(); });
    if (auto *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(guiApp, &QGuiApplication::screenAdded, this, [this](QScreen *) {
            refreshDesktopScreens();
            emitDesktopShareScreen();
        });
        connect(guiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
            refreshDesktopScreens();
            emitDesktopShareScreen();
        });
    }

    m_chatPanel = new QFrame(this);
    m_chatPanel->setObjectName("wdg_chatPanel");
    m_chatPanel->setFixedWidth(320);
    auto *chatLayout = new QVBoxLayout(m_chatPanel);
    chatLayout->setContentsMargins(12, 12, 12, 12);
    chatLayout->setSpacing(10);

    auto *chatHeader = new QHBoxLayout;
    auto *chatTitle = new QLabel("房间聊天", m_chatPanel);
    chatTitle->setObjectName("lb_chatTitle");
    auto *chatClose = new QToolButton(m_chatPanel);
    chatClose->setObjectName("pb_chatClose");
    chatClose->setText("X");
    chatClose->setToolTip("收起聊天");
    chatHeader->addWidget(chatTitle);
    chatHeader->addStretch(1);
    chatHeader->addWidget(chatClose);
    chatLayout->addLayout(chatHeader);

    m_chatHistory = new QTextBrowser(m_chatPanel);
    m_chatHistory->setObjectName("tb_chatHistory");
    // User names are in-document links. Handle them through anchorClicked only;
    // otherwise QTextBrowser navigates to the profile URL and replaces the chat history.
    m_chatHistory->setOpenLinks(false);
    m_chatHistory->setOpenExternalLinks(false);
    m_chatHistory->setReadOnly(true);
    m_chatHistory->setPlaceholderText("还没有消息");
    chatLayout->addWidget(m_chatHistory, 1);
    connect(m_chatHistory, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() != "profile") {
            return;
        }
        bool validUserId = false;
        const int userId = url.path().mid(1).toInt(&validUserId);
        if (validUserId && userId > 0) {
            Q_EMIT SIG_chatUserRequested(userId);
        }
    });

    m_chatInput = new QTextEdit(m_chatPanel);
    m_chatInput->setObjectName("te_chatInput");
    m_chatInput->setPlaceholderText("输入消息");
    m_chatInput->setAcceptRichText(false);
    m_chatInput->setMaximumHeight(82);
    m_chatInput->installEventFilter(this);
    chatLayout->addWidget(m_chatInput);

    auto *sendLayout = new QHBoxLayout;
    auto *sendHint = new QLabel("Enter 发送", m_chatPanel);
    sendHint->setObjectName("lb_chatHint");
    auto *sendButton = new QPushButton("发送", m_chatPanel);
    sendButton->setObjectName("pb_chatSend");
    sendButton->setFixedSize(76, 34);
    sendLayout->addWidget(sendHint);
    sendLayout->addStretch(1);
    sendLayout->addWidget(sendButton);
    chatLayout->addLayout(sendLayout);

    ui->horizontalLayout_3->insertWidget(1, m_chatPanel);
    ui->horizontalLayout_3->setStretch(1, 0);
    ui->horizontalLayout_3->setStretch(2, 0);
    setChatPanelVisible(false);
    connect(chatClose, &QToolButton::clicked, this,
            [this]() { setChatPanelVisible(false); });
    connect(sendButton, &QPushButton::clicked, this, &RoomDialog::sendChatMessage);

    const int chatButtonIndex = ui->horizontalLayout_2->indexOf(ui->pb_quit);
    m_chatButton = new QPushButton("聊天", this);
    m_chatButton->setObjectName("pb_chatToggle");
    m_chatButton->setFixedSize(76, 36);
    m_chatButton->setToolTip("打开房间聊天");
    ui->horizontalLayout_2->insertWidget(chatButtonIndex, m_chatButton);
    connect(m_chatButton, &QPushButton::clicked, this,
            [this]() { setChatPanelVisible(!m_chatPanel->isVisible()); });

    m_userLayout = new QVBoxLayout;

    // 用户列表垂直布局
    ui->wdg_userList->setLayout(m_userLayout);

    m_userLayout->setContentsMargins(0,0,0,0);
    m_userLayout->setSpacing(5);
    // 可以通过addwedgit添加空间
    // m_userLayout->addWidget();

    // 添加测试用户
    //    UserShow* user1 = new UserShow;
    //    user1->slot_setInfo(1, "路人甲");
    //    UserShow* user2 = new UserShow;
    //    user2->slot_setInfo(2, "路人已");
    //    UserShow* user3 = new UserShow;
    //    user3->slot_setInfo(3, "路人丙");
    //    UserShow* user4 = new UserShow;
    //    user4->slot_setInfo(4, "路人丁");
    //    UserShow* user5 = new UserShow;
    //    user5->slot_setInfo(5, "路人六");

    //    slot_addUser(user1);
    //    slot_addUser(user2);
    //    slot_addUser(user3);
    //    slot_addUser(user4);
    //    slot_addUser(user5);
}

void RoomDialog::slot_setLatency(int latencyMs)
{
    m_latencyLabel->setLatency(latencyMs);
}

void RoomDialog::slot_setDesktopSharePending(int frameRate, int bitRate)
{
    const int megaBits = qMax(1, bitRate / 1000000);
    setDesktopShareStatusText(QString("共享 准备中 | 目标 %1 FPS | 目标 %2 Mbps")
                              .arg(frameRate).arg(megaBits),
                              "正在初始化桌面采集和 H.264 编码器");
}

void RoomDialog::slot_setDesktopShareStatus(int width, int height, int frameRate,
                                            int bitRate, const QString &encoderName)
{
    QString encoder = "H.264";
    if (encoderName == "h264_nvenc") {
        encoder = "NVENC";
    } else if (encoderName == "libopenh264") {
        encoder = "OpenH264";
    } else if (encoderName == "h264_mf") {
        encoder = "Media Foundation";
    }
    const int megaBits = qMax(1, bitRate / 1000000);
    setDesktopShareStatusText(QString("共享 %1x%2 | 目标 %3 FPS | 目标 %4 Mbps | %5")
                              .arg(width).arg(height).arg(frameRate).arg(megaBits)
                              .arg(encoder),
                              "显示的是实际编码分辨率、目标帧率、目标码率和当前 H.264 编码器");
}

void RoomDialog::slot_setDesktopShareRuntimeStats(int width, int height,
                                                   int targetFrameRate, int bitRate,
                                                   const QString &encoderName,
                                                   int capturedFrames, int encodedFrames,
                                                   int sentFrames, int droppedFrames)
{
    QString encoder = "H.264";
    if (encoderName == "h264_nvenc") {
        encoder = "NVENC";
    } else if (encoderName == "h264_mf") {
        encoder = "Media Foundation";
    } else if (encoderName == "libopenh264") {
        encoder = "OpenH264（软件）";
    }

    const int megaBits = qMax(1, bitRate / 1000000);
    setDesktopShareStatusText(
                QString("共享 %1x%2 | 目标 %3 FPS / %4 Mbps | 实际 采集 %5 编码 %6 发送 %7 FPS | 丢帧 %8 | %9")
                .arg(width).arg(height).arg(targetFrameRate).arg(megaBits)
                .arg(capturedFrames).arg(encodedFrames).arg(sentFrames)
                .arg(droppedFrames).arg(encoder),
                "实际 FPS 每秒更新：采集为送入编码器的画面数；编码为 H.264 输出数；发送为写入视频连接的帧数；丢帧表示为保持低延迟而替换或放弃的旧画面。");
}

void RoomDialog::slot_clearDesktopShareStatus()
{
    setDesktopShareStatusText("共享 未开始", "桌面共享的实际编码状态");
}

RoomDialog::~RoomDialog()
{
    delete ui;
}

void RoomDialog::slot_setRoomID(int roomid)
{
    ui->lb_title->setText(QString("房间号[%1]").arg(roomid));
}

void RoomDialog::slot_addUser(QWidget *user)
{
    m_userLayout->addWidget(user);
}

void RoomDialog::slot_removeUser(QWidget *user)
{
    user->hide();
    m_userLayout->removeWidget(user);
    delete user;
}

void RoomDialog::slot_setRoomClear()
{
    m_localUserId = 0;
    m_bigUserPinnedByUser = false;
    ui->wdg_bigShow->slot_clearVideo();
    // 设置ui
    ui->cb_audio->setChecked(false);
    setSystemAudioControlsEnabled(false);
    ui->cb_capture->setChecked(false);
    ui->cb_desk->setChecked(false);
    // 发信号
    Q_EMIT SIG_audioClose();
    Q_EMIT SIG_captureClose();
    Q_EMIT SIG_deskClose();
    slot_clearDesktopShareStatus();
    if (m_chatHistory) {
        m_chatHistory->clear();
    }
    m_unreadChatCount = 0;
    updateChatButton();
    setChatPanelVisible(false);
}

void RoomDialog::slot_appendChatMessage(int userId, const QString &name, const QString &message,
                                        bool ownMessage)
{
    if (!m_chatHistory || message.trimmed().isEmpty()) {
        return;
    }

    const QString escapedName = (name.trimmed().isEmpty() ? QString("未设置昵称") : name.trimmed())
            .toHtmlEscaped();
    const QString profileLink = QStringLiteral("<a href=\"profile:///%1\" "
                                               "style=\"color:%2;font-weight:600;"
                                               "text-decoration:none;\">%3</a>")
            .arg(userId)
            .arg(ownMessage ? "#FF7A80" : "#E6E8EB")
            .arg(escapedName);
    const QString escapedMessage = message.toHtmlEscaped().replace("\n", "<br/>");
    const QString timestamp = QTime::currentTime().toString("HH:mm");
    const QString messageColor = ownMessage ? "#FFE7E8" : "#D7DBE0";
    m_chatHistory->append(QStringLiteral(
        "<div style=\"margin:2px 2px 12px 2px;\">"
        "%1"
        "<span style=\"color:#777F8B;margin-left:8px;\">%2</span><br/>"
        "<span style=\"color:%3;\">%4</span></div>")
        .arg(profileLink, timestamp, messageColor, escapedMessage));
    m_chatHistory->moveCursor(QTextCursor::End);
    m_chatHistory->verticalScrollBar()->setValue(m_chatHistory->verticalScrollBar()->maximum());

    if (!ownMessage && !m_chatPanel->isVisible()) {
        ++m_unreadChatCount;
        updateChatButton();
    }
}

void RoomDialog::setChatPanelVisible(bool visible)
{
    if (!m_chatPanel) {
        return;
    }
    m_chatPanel->setVisible(visible);
    if (visible) {
        m_unreadChatCount = 0;
        updateChatButton();
        if (m_chatInput) {
            m_chatInput->setFocus();
        }
    }
}

void RoomDialog::sendChatMessage()
{
    if (!m_chatInput) {
        return;
    }
    const QString message = m_chatInput->toPlainText().trimmed();
    if (message.isEmpty()) {
        return;
    }
    Q_EMIT SIG_chatMessageSend(message);
}

void RoomDialog::slot_clearChatInput()
{
    if (!m_chatInput) {
        return;
    }
    m_chatInput->clear();
}

void RoomDialog::updateChatButton()
{
    if (!m_chatButton) {
        return;
    }
    m_chatButton->setText(m_unreadChatCount > 0
                          ? QString("聊天 %1").arg(qMin(m_unreadChatCount, 99))
                          : QString("聊天"));
}

bool RoomDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_systemAudioCombo &&
            (event->type() == QEvent::Show || event->type() == QEvent::MouseButtonPress ||
             event->type() == QEvent::KeyPress)) {
        refreshSystemAudioDevices();
    }
    if (watched == m_chatInput && event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
                !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            sendChatMessage();
            return true;
        }
    }
    return CustomMoveDialog::eventFilter(watched, event);
}

void RoomDialog::refreshSystemAudioDevices()
{
    if (!m_systemAudioCombo) {
        return;
    }

    const QString selectedId = m_systemAudioCombo->currentData().toString();
    const QList<SystemAudioDevice> outputDevices = SystemAudio_Read::availableOutputDevices();
    QString refreshedId;
    {
        const QSignalBlocker blocker(m_systemAudioCombo);
        m_systemAudioCombo->clear();
        m_systemAudioCombo->addItem("跟随系统默认设备", QString());
        int selectedIndex = 0;
        for (const SystemAudioDevice &device : outputDevices) {
            QString label = device.name;
            if (device.isDefault) {
                label += " (默认)";
            }
            m_systemAudioCombo->addItem(label, device.id);
            if (!selectedId.isEmpty() && selectedId == device.id) {
                selectedIndex = m_systemAudioCombo->count() - 1;
            }
        }
        m_systemAudioCombo->setCurrentIndex(selectedIndex);
        refreshedId = m_systemAudioCombo->currentData().toString();
    }
    if (refreshedId != selectedId) {
        Q_EMIT SIG_systemAudioDeviceChanged(refreshedId);
    }
    if (outputDevices.isEmpty()) {
        m_systemAudioCombo->setToolTip("未找到活动播放设备，将在开启时尝试使用系统默认输出");
    } else {
        m_systemAudioCombo->setToolTip("选择要采集的播放设备；默认会跟随 Windows 默认输出");
    }
}

void RoomDialog::emitDesktopShareSettings()
{
    if (!m_deskResolutionCombo || !m_deskFrameRateCombo) {
        return;
    }

    const QSize resolution = m_deskResolutionCombo->currentData().toSize();
    const int frameRate = m_deskFrameRateCombo->currentData().toInt();
    if (resolution.width() > 0 && resolution.height() > 0 && frameRate > 0) {
        Q_EMIT SIG_desktopShareSettingsChanged(resolution.width(), resolution.height(), frameRate);
    }
}

QString RoomDialog::desktopShareScreenName() const
{
    return m_deskScreenCombo ? m_deskScreenCombo->currentData().toString() : QString();
}

void RoomDialog::emitDesktopShareScreen()
{
    const QString screenName = desktopShareScreenName();
    if (!screenName.isEmpty()) {
        Q_EMIT SIG_desktopShareScreenChanged(screenName);
    }
}

void RoomDialog::refreshDesktopScreens()
{
    if (!m_deskScreenCombo) {
        return;
    }

    const QString previousScreen = desktopShareScreenName();
    QSignalBlocker blocker(m_deskScreenCombo);
    m_deskScreenCombo->clear();

    const QList<QScreen *> screens = QGuiApplication::screens();
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    for (QScreen *screen : screens) {
        if (!screen) {
            continue;
        }
        const QString connectorName = screen->name();
        const QString modelName = screen->model().trimmed();
        const QString displayName = modelName.isEmpty() ? connectorName : modelName;
        const QSize size = screen->size();
        QString label = QString("%1 %2x%3")
                .arg(displayName.isEmpty() ? "显示器" : displayName)
                .arg(size.width()).arg(size.height());
        if (screen == primaryScreen) {
            label += " 主屏";
        }
        m_deskScreenCombo->addItem(label, connectorName);
    }

    int selectedIndex = m_deskScreenCombo->findData(previousScreen);
    if (selectedIndex < 0 && primaryScreen) {
        selectedIndex = m_deskScreenCombo->findData(primaryScreen->name());
    }
    if (selectedIndex >= 0) {
        m_deskScreenCombo->setCurrentIndex(selectedIndex);
    }
    m_deskScreenCombo->setEnabled(m_deskScreenCombo->count() > 0);
}

void RoomDialog::setDesktopShareStatusText(const QString &text, const QString &toolTip)
{
    if (!m_deskStatusLabel || m_deskStatusLabel->text() == text) {
        return;
    }
    m_deskStatusLabel->setText(text);
    m_deskStatusLabel->setToolTip(toolTip);
}

void RoomDialog::setTopStatusVisible(bool visible)
{
    m_topStatusVisible = visible;
    if (m_mediaStatus) {
        m_mediaStatus->setVisible(visible);
    }
    if (m_deskStatusLabel) {
        m_deskStatusLabel->setVisible(visible);
    }
    if (m_latencyLabel) {
        m_latencyLabel->setToolTip(visible
                ? "每 5 秒自动测量到会议服务器的连接延迟；点击隐藏共享状态"
                : "每 5 秒自动测量到会议服务器的连接延迟；点击显示共享状态");
    }
}

UserShow *RoomDialog::userShowForId(int userId) const
{
    if (!m_userLayout) {
        return nullptr;
    }

    for (int index = 0; index < m_userLayout->count(); ++index) {
        UserShow *user = qobject_cast<UserShow *>(m_userLayout->itemAt(index)->widget());
        if (user && user->m_id == userId) {
            return user;
        }
    }
    return nullptr;
}

void RoomDialog::setBigUserFromUserShow(UserShow *user, const QImage &preview)
{
    if (!user) {
        return;
    }

    ui->wdg_bigShow->slot_setInfo(user->m_id, user->m_name);
    const QImage avatar = user->m_avatarImg.isNull()
            ? user->m_defaultImg : user->m_avatarImg;
    ui->wdg_bigShow->slot_setAvatarImage(avatar);
    ui->wdg_bigShow->slot_setPreviewImage(preview.isNull() ? user->m_img : preview);
}

void RoomDialog::slot_setBigImage(int userid, QImage &img)
{
    // Joining a room initially selects the local user. Switch to the first
    // remote participant that actually sends a frame, unless the user has
    // explicitly chosen a participant from the side list.
    if (userid != m_localUserId && !m_bigUserPinnedByUser &&
            ui->wdg_bigShow->m_id == m_localUserId) {
        setBigUserFromUserShow(userShowForId(userid), img);
    }

    if (ui->wdg_bigShow->m_id == userid) {
        ui->wdg_bigShow->slot_setImage(img);
    }
}

void RoomDialog::slot_setBigImageInfo(int userid, QString name)
{
    UserShow *user = qobject_cast<UserShow *>(QObject::sender());
    if (!user || user->m_id != userid) {
        user = userShowForId(userid);
    }
    if (!user) {
        ui->wdg_bigShow->slot_setInfo(userid, name);
        return;
    }

    m_bigUserPinnedByUser = true;
    setBigUserFromUserShow(user, user->m_img);
}

void RoomDialog::slot_setBigUser(int userid, const QString &name, const QImage &avatar)
{
    m_localUserId = userid;
    m_bigUserPinnedByUser = false;
    ui->wdg_bigShow->slot_setInfo(userid, name);
    ui->wdg_bigShow->slot_setAvatarImage(avatar);
    ui->wdg_bigShow->slot_setPreviewImage(avatar);
}

void RoomDialog::slot_setBigAvatar(int userid, const QImage &avatar)
{
    if (ui->wdg_bigShow->m_id == userid) {
        ui->wdg_bigShow->slot_setAvatarImage(avatar);
    }
}

void RoomDialog::slot_clearBigImage(int userid)
{
    if (ui->wdg_bigShow->m_id == userid) {
        ui->wdg_bigShow->slot_clearVideo();
    }
}

void RoomDialog::slot_setCaptureState(bool active)
{
    if (!active) {
        ui->cb_capture->setChecked(false);
    }
}

void RoomDialog::slot_setSystemAudioError(const QString &message)
{
    if (!m_systemAudioCheck) {
        return;
    }
    const QString detail = message.trimmed().isEmpty()
            ? QString("把选定播放设备的系统声音与麦克风一起发送；建议使用耳机避免回声")
            : QString("系统声音采集失败，将自动重试：%1").arg(message);
    m_systemAudioCheck->setToolTip(detail);
}

void RoomDialog::on_pb_close_clicked()
{
    on_pb_quit_clicked();
}


void RoomDialog::on_pb_max_clicked()
{
    this->slot_showMax();
}


void RoomDialog::on_pb_min_clicked()
{
    this->slot_showMin();
}

// 列表的显示隐藏
void RoomDialog::on_pb_hide_clicked()
{
    if(ui->sa_userList->isHidden()){
        ui->sa_userList->show();
    }else{
        ui->sa_userList->hide();
    }
}

#include <QMessageBox>
void RoomDialog::on_pb_quit_clicked()
{
    if(QMessageBox::question(this, "tips", "是否退出房间?")
            == QMessageBox::Yes)
    {
        Q_EMIT SIG_quitRoom();
        this->close();
    }
}


void RoomDialog::on_cb_audio_clicked()
{
    if(ui->cb_audio->isChecked())    // 判断是否被勾选
    {
        //        ui->cb_audio->setChecked(false);

        // RoomDialog -> sig ->ckernal -> slot ->audio 开关
        // 发送开启信号
        Q_EMIT SIG_audioOpen();
    }else{
        //        ui->cb_audio->setChecked(true);

        // 发送信息 RoomDialog <- ckernal(audio video)
        Q_EMIT SIG_audioClose();
    }
}


void RoomDialog::on_cb_capture_clicked()
{
    if(ui->cb_capture->isChecked()){
        //        ui->cb_capture->setChecked(false);
        ui->cb_desk->setChecked(false);
        setSystemAudioControlsEnabled(false);
        // 发信号
        // 摄像头开启，桌面关闭
        Q_EMIT SIG_captureOpen();
        Q_EMIT SIG_deskClose();


    }else{
        //        ui->cb_capture->setChecked(true);

        // 发信号
        // 摄像头关闭
        Q_EMIT SIG_captureClose();
    }
}


void RoomDialog::on_cb_desk_clicked()
{
    if(ui->cb_desk->isChecked()){
        //       ui->cb_desk->setChecked(false);
        ui->cb_capture->setChecked(false);
        setSystemAudioControlsEnabled(true);

        // 发信号
        // 桌面开启，摄像头关闭
        emitDesktopShareSettings();
        Q_EMIT SIG_captureClose();
        Q_EMIT SIG_deskOpen();
    }else{
        //      ui->cb_desk->setChecked(true);

        setSystemAudioControlsEnabled(false);

        // 发信号
        // 桌面关闭
        Q_EMIT SIG_deskClose();
    }
}

void RoomDialog::setSystemAudioChecked(bool checked)
{
    if (!m_systemAudioCheck) {
        return;
    }
    const bool changed = m_systemAudioCheck->isChecked() != checked;
    const QSignalBlocker blocker(m_systemAudioCheck);
    m_systemAudioCheck->setChecked(checked);
    if (changed) {
        Q_EMIT SIG_systemAudioStateChanged(checked);
    }
}

void RoomDialog::setSystemAudioControlsEnabled(bool enabled)
{
    if (!enabled) {
        setSystemAudioChecked(false);
    }
    if (m_systemAudioCheck) {
        m_systemAudioCheck->setEnabled(enabled);
    }
    if (m_systemAudioCombo) {
        m_systemAudioCombo->setEnabled(enabled);
    }
}


void RoomDialog::on_cbx_moji_currentIndexChanged(int index)
{
    if(index == 1 || index == 2)
    {
        Q_EMIT SIG_setFunnyPic(index);
    }
}


void RoomDialog::slot_setMediaStatus(const QString &text)
{
    m_mediaStatus->setText(text);
}

void RoomDialog::slot_setRoomName(const QString &id)
{
    ui->lb_title->setMaximumWidth(300);
    ui->lb_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui->lb_title->setText(QStringLiteral("房间[%1]").arg(id));
    ui->lb_title->setToolTip(id);
}
