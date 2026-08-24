#include "roomdialog.h"
#include "ui_roomdialog.h"

RoomDialog::RoomDialog(QWidget *parent) :
    CustomMoveDialog(parent),
    ui(new Ui::RoomDialog),
    m_latencyLabel(new LatencyLabel(this))
{
    ui->setupUi(this);

    setWindowTitle("Meeting");
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
    ui->horizontalLayout_2->setSpacing(18);
    ui->sa_userList->setFixedWidth(264);
    ui->pb_quit->setFixedSize(86, 36);
    ui->cbx_moji->setFixedWidth(108);
    ui->pb_hide->setToolTip("显示或隐藏参会者");
    ui->pb_min->setToolTip("最小化");
    ui->pb_max->setToolTip("最大化");
    ui->pb_close->setToolTip("退出会议");

    ui->horizontalLayout->insertWidget(0, m_latencyLabel);

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

