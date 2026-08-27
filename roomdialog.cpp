#include "roomdialog.h"
#include "ui_roomdialog.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTextCursor>
#include <QTime>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

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

    int desktopControlIndex = ui->horizontalLayout_2->indexOf(ui->cb_desk) + 1;
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, qualityLabel);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, m_deskResolutionCombo);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex++, frameRateLabel);
    ui->horizontalLayout_2->insertWidget(desktopControlIndex, m_deskFrameRateCombo);
    connect(m_deskResolutionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emitDesktopShareSettings(); });
    connect(m_deskFrameRateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emitDesktopShareSettings(); });

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
    // 设置ui
    ui->cb_audio->setChecked(false);
    ui->cb_capture->setChecked(false);
    ui->cb_desk->setChecked(false);
    // 发信号
    Q_EMIT SIG_audioClose();
    Q_EMIT SIG_captureClose();
    Q_EMIT SIG_deskClose();
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

void RoomDialog::slot_setBigImage(int userid, QImage &img)
{
    if(ui->wdg_bigShow->m_id == userid)
    {
        ui->wdg_bigShow->slot_setImage(img);
    }
}
#include <QDebug>
void RoomDialog::slot_setBigImageInfo(int userid, QString name)
{
    ui->wdg_bigShow->slot_setInfo(userid, name);
    UserShow *user = qobject_cast<UserShow *>(QObject::sender());
    if (!user) {
        return;
    }

    const QImage avatar = user->m_avatarImg.isNull()
            ? user->m_defaultImg : user->m_avatarImg;
    ui->wdg_bigShow->slot_setAvatarImage(avatar);
    ui->wdg_bigShow->slot_setPreviewImage(user->m_img);
}

void RoomDialog::slot_setBigUser(int userid, const QString &name, const QImage &avatar)
{
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

        // 发信号
        // 桌面开启，摄像头关闭
        emitDesktopShareSettings();
        Q_EMIT SIG_captureClose();
        Q_EMIT SIG_deskOpen();
    }else{
        //      ui->cb_desk->setChecked(true);

        // 发信号
        // 桌面关闭
        Q_EMIT SIG_deskClose();
    }
}


void RoomDialog::on_cbx_moji_currentIndexChanged(int index)
{
    if(index == 1 || index == 2)
    {
        Q_EMIT SIG_setFunnyPic(index);
    }
}

