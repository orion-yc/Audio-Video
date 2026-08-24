#include "demodialog.h"
#include "ui_demodialog.h"

#include <QIcon>
#include <QPixmap>

demoDialog::demoDialog(QWidget *parent)
    : CustomMoveDialog(parent)
    , ui(new Ui::demoDialog)
    , m_latencyLabel(new LatencyLabel(this))
{
    ui->setupUi(this);

    setWindowTitle("Meeting");
    resize(540, 640);
    setMinimumSize(480, 560);

    ui->wdg_title->setFixedHeight(52);
    ui->wdg_user->setFixedHeight(96);
    ui->wdg_room->setFixedHeight(158);
    ui->horizontalLayout->setContentsMargins(14, 8, 8, 8);
    ui->horizontalLayout_2->setContentsMargins(22, 16, 22, 16);
    ui->horizontalLayout_2->setSpacing(12);
    ui->horizontalLayout_3->setContentsMargins(22, 18, 22, 18);
    ui->horizontalLayout_3->setSpacing(12);

    const QSize actionSize(150, 112);
    ui->pb_join->setFixedSize(actionSize);
    ui->pb_create->setFixedSize(actionSize);
    ui->pb_history->setFixedSize(actionSize);
    ui->pb_join->setIconSize(QSize(30, 30));
    ui->pb_create->setIconSize(QSize(30, 30));
    ui->pb_history->setIconSize(QSize(30, 30));
    ui->pb_history->setEnabled(false);
    ui->pb_history->setToolTip("房间记录暂不可用");
    ui->pb_setup->setIcon(QIcon());
    ui->pb_setup->setText("设置");
    ui->pb_setup->setFixedSize(52, 32);
    ui->pb_setup->setToolTip("个人设置");
    connect(ui->pb_setup, &QPushButton::clicked, this, &demoDialog::on_pb_icon_clicked);

    ui->horizontalLayout->insertWidget(0, m_latencyLabel);

    this->setWindowFlags(Qt::FramelessWindowHint);
}

void demoDialog::slot_setLatency(int latencyMs)
{
    m_latencyLabel->setLatency(latencyMs);
}

demoDialog::~demoDialog()
{
    delete ui;
}

void demoDialog::closeEvent(QCloseEvent *event)
{
    if(QMessageBox::question(this, "提示", "是否关闭") == QMessageBox::Yes)
    {
        // 关闭
        // event->accept();
        Q_EMIT SIG_close();
    }
    else{
        // 忽略
        event->ignore();
    }
}

// 最小化
void demoDialog::on_pb_min_clicked()
{
    this->showMinimized();
}

// 关闭
void demoDialog::on_pb_close_clicked()
{
    this->close();
}

// 创建会议
void demoDialog::on_pb_create_clicked()
{
    Q_EMIT SIG_createRoom();
}

// 加入会议
void demoDialog::on_pb_join_clicked()
{
    Q_EMIT SIG_joinRoom();
}

// 设置属性 ui和成员
void demoDialog::slot_setInfo(QString name, int iconid)
{
    m_name = name;
    m_iconid = iconid;

    ui->lb_name->setText(m_name);
    // 资源引用 :/tx/2.png
    ui->pb_icon->setIcon(QIcon(QString(":/tx/%1.png").arg(iconid)));
}

void demoDialog::slot_setAvatarImage(const QImage &avatar)
{
    if (avatar.isNull()) {
        return;
    }
    ui->pb_icon->setIcon(QIcon(QPixmap::fromImage(avatar)));
}


void demoDialog::on_pb_icon_clicked()
{
    // 发送信号
    Q_EMIT SIG_setUser();
}

void demoDialog::slot_setPushButton_enable(bool flag)
{
    ui->pb_create->setEnabled(flag);
    ui->pb_join->setEnabled(flag);
}

