#include "setuserdialog.h"
#include "ui_setuserdialog.h"

#include "packdef.h"

#include <QBuffer>
#include <QComboBox>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>

namespace {
constexpr int kBuiltInAvatarCount = 36;
constexpr int kAvatarEdge = 256;
}

SetUserDialog::SetUserDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SetUserDialog),
    m_uploadAvatarButton(new QPushButton("上传头像", this)),
    m_iconid(0),
    m_uploadedAvatarIndex(-1),
    m_hasCustomAvatar(false),
    m_avatarChanged(false),
    m_blockAvatarSelection(false)
{
    ui->setupUi(this);

    setWindowTitle("个人设置");
    setFixedSize(440, 390);
    ui->gridLayout->parentWidget()->setGeometry(28, 24, 384, 226);
    ui->gridLayout->setHorizontalSpacing(18);
    ui->gridLayout->setVerticalSpacing(14);
    ui->gridLayout->setColumnStretch(0, 0);
    ui->gridLayout->setColumnStretch(1, 1);
    ui->cbx_icon->setFixedSize(100, 68);
    ui->cbx_icon->setIconSize(QSize(62, 62));
    ui->pb_clear->setGeometry(224, 326, 88, 38);
    ui->pb_commit->setGeometry(324, 326, 88, 38);
    ui->le_name->setPlaceholderText("请输入昵称");
    ui->le_feeling->setPlaceholderText("请输入个性签名");
    ui->cbx_icon->setToolTip("选择内置头像");

    m_uploadAvatarButton->setObjectName("pb_uploadAvatar");
    m_uploadAvatarButton->setGeometry(28, 270, 180, 38);

    for (int i = 0; i < kBuiltInAvatarCount; ++i) {
        ui->cbx_icon->addItem(QIcon(QString(":/tx/%1.png").arg(i)), "");
    }

    connect(m_uploadAvatarButton, &QPushButton::clicked,
            this, &SetUserDialog::on_pb_uploadAvatar_clicked);
    connect(ui->cbx_icon, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SetUserDialog::on_cbx_icon_currentIndexChanged);
}

SetUserDialog::~SetUserDialog()
{
    delete ui;
}

void SetUserDialog::on_pb_commit_clicked()
{
    int iconid = ui->cbx_icon->currentIndex();
    if (iconid == m_uploadedAvatarIndex) {
        iconid = m_iconid;
    }
    const QString name = ui->le_name->text();
    const QString feeling = ui->le_feeling->text();

    if (m_iconid == iconid && m_name == name && m_feeling == feeling && !m_avatarChanged) {
        return;
    }

    if (name.isEmpty() || name.length() > 20) {
        QMessageBox::about(this, "提示", "昵称非法");
        return;
    }

    if (feeling.isEmpty() || feeling.length() > 20) {
        QMessageBox::about(this, "提示", "签名非法");
        return;
    }

    hide();
    Q_EMIT SIG_userSetCommit(iconid, name, feeling,
                             m_hasCustomAvatar ? m_avatarData : QByteArray(),
                             m_avatarChanged);

    slot_setInfo(iconid, name, feeling);
    m_avatarChanged = false;
}

void SetUserDialog::on_pb_clear_clicked()
{
    ui->le_feeling->clear();
    ui->le_name->clear();
}

void SetUserDialog::slot_setInfo(int iconid, QString name, QString feeling)
{
    m_iconid = qBound(0, iconid, kBuiltInAvatarCount - 1);
    m_name = name;
    m_feeling = feeling;

    if (!m_hasCustomAvatar) {
        m_blockAvatarSelection = true;
        ui->cbx_icon->setCurrentIndex(m_iconid);
        m_blockAvatarSelection = false;
    }
    ui->le_feeling->setText(m_feeling);
    ui->le_name->setText(m_name);
}

QByteArray SetUserDialog::encodeAvatar(const QImage &avatar) const
{
    if (avatar.isNull()) {
        return QByteArray();
    }

    const QImage scaled = avatar.scaled(kAvatarEdge, kAvatarEdge,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
    const QRect crop((scaled.width() - kAvatarEdge) / 2,
                     (scaled.height() - kAvatarEdge) / 2,
                     kAvatarEdge, kAvatarEdge);
    const QImage square = scaled.copy(crop).convertToFormat(QImage::Format_RGB32);

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    if (!square.save(&buffer, "JPG", 86) || data.size() > DEF_AVATAR_IMAGE_MAX_SIZE) {
        return QByteArray();
    }
    return data;
}

void SetUserDialog::setCustomAvatar(const QImage &avatar, bool changed)
{
    const QByteArray data = encodeAvatar(avatar);
    if (data.isEmpty()) {
        QMessageBox::warning(this, "提示", "头像处理失败，请选择一张 JPG、PNG 或 BMP 图片");
        return;
    }

    m_blockAvatarSelection = true;
    const QIcon icon(QPixmap::fromImage(avatar.scaled(62, 62, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation)));
    if (m_uploadedAvatarIndex < 0) {
        m_uploadedAvatarIndex = ui->cbx_icon->count();
        ui->cbx_icon->addItem(icon, "已上传");
    } else {
        ui->cbx_icon->setItemIcon(m_uploadedAvatarIndex, icon);
        ui->cbx_icon->setItemText(m_uploadedAvatarIndex, "已上传");
    }
    ui->cbx_icon->setCurrentIndex(m_uploadedAvatarIndex);
    m_blockAvatarSelection = false;

    m_avatarData = data;
    m_hasCustomAvatar = true;
    m_avatarChanged = changed;
}

void SetUserDialog::on_pb_uploadAvatar_clicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "选择头像", QString(), "图片文件 (*.jpg *.jpeg *.png *.bmp)");
    if (path.isEmpty()) {
        return;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage avatar = reader.read();
    if (avatar.isNull()) {
        QMessageBox::warning(this, "提示", "无法读取该图片，请选择 JPG、PNG 或 BMP 图片");
        return;
    }
    setCustomAvatar(avatar, true);
}

void SetUserDialog::on_cbx_icon_currentIndexChanged(int index)
{
    if (m_blockAvatarSelection || index == m_uploadedAvatarIndex) {
        return;
    }
    if (m_hasCustomAvatar) {
        m_hasCustomAvatar = false;
        m_avatarData.clear();
        m_avatarChanged = true;
    }
}

void SetUserDialog::slot_setAvatarImage(const QImage &avatar)
{
    setCustomAvatar(avatar, false);
}
