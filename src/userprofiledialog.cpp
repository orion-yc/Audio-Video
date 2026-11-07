#include "userprofiledialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

UserProfileDialog::UserProfileDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("UserProfileDialog");
    setWindowTitle("成员资料 - Audio-Video");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(false);
    setFixedSize(360, 348);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(22, 22, 22, 18);
    rootLayout->setSpacing(14);

    auto *profileHeader = new QHBoxLayout;
    profileHeader->setSpacing(14);
    m_avatarLabel = new QLabel(this);
    m_avatarLabel->setObjectName("lb_profileAvatar");
    m_avatarLabel->setFixedSize(82, 82);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    profileHeader->addWidget(m_avatarLabel);

    auto *headerTextLayout = new QVBoxLayout;
    headerTextLayout->setSpacing(5);
    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName("lb_profileName");
    m_nameLabel->setWordWrap(true);
    m_memberTypeLabel = new QLabel(this);
    m_memberTypeLabel->setObjectName("lb_profileMemberType");
    headerTextLayout->addStretch(1);
    headerTextLayout->addWidget(m_nameLabel);
    headerTextLayout->addWidget(m_memberTypeLabel);
    headerTextLayout->addStretch(1);
    profileHeader->addLayout(headerTextLayout, 1);
    rootLayout->addLayout(profileHeader);

    auto *detailsFrame = new QFrame(this);
    detailsFrame->setObjectName("wdg_profileDetails");
    auto *detailsLayout = new QVBoxLayout(detailsFrame);
    detailsLayout->setContentsMargins(14, 12, 14, 12);
    detailsLayout->setSpacing(8);

    auto *idCaption = new QLabel("用户 ID", detailsFrame);
    idCaption->setObjectName("lb_profileCaption");
    m_userIdValueLabel = new QLabel(detailsFrame);
    m_userIdValueLabel->setObjectName("lb_profileValue");
    detailsLayout->addWidget(idCaption);
    detailsLayout->addWidget(m_userIdValueLabel);

    auto *feelingCaption = new QLabel("个性签名", detailsFrame);
    feelingCaption->setObjectName("lb_profileCaption");
    m_feelingValueLabel = new QLabel(detailsFrame);
    m_feelingValueLabel->setObjectName("lb_profileFeeling");
    m_feelingValueLabel->setWordWrap(true);
    detailsLayout->addSpacing(4);
    detailsLayout->addWidget(feelingCaption);
    detailsLayout->addWidget(m_feelingValueLabel);
    detailsLayout->addStretch(1);
    rootLayout->addWidget(detailsFrame, 1);

    auto *closeButton = new QPushButton("关闭", this);
    closeButton->setObjectName("pb_profileClose");
    closeButton->setFixedSize(82, 34);
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);
    rootLayout->addLayout(buttonLayout);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
}

void UserProfileDialog::setProfile(int userId, const QString &name, const QImage &avatar,
                                   const QString &feeling, bool currentUser)
{
    m_userId = userId;
    m_nameLabel->setText(name.trimmed().isEmpty() ? "未设置昵称" : name.trimmed());
    m_memberTypeLabel->setText(currentUser ? "当前账号" : "房间成员");
    m_userIdValueLabel->setText(QString::number(userId));
    m_feelingValueLabel->setText(feeling.trimmed().isEmpty()
                                 ? "该成员未公开个性签名"
                                 : feeling.trimmed());
    updateAvatar(avatar);
}

void UserProfileDialog::slot_setAvatar(const QImage &avatar)
{
    if (!avatar.isNull()) {
        updateAvatar(avatar);
    }
}

int UserProfileDialog::userId() const
{
    return m_userId;
}

void UserProfileDialog::updateAvatar(const QImage &avatar)
{
    if (avatar.isNull()) {
        m_avatarLabel->clear();
        return;
    }

    const QPixmap pixmap = QPixmap::fromImage(avatar.scaled(m_avatarLabel->size(),
                                                             Qt::KeepAspectRatioByExpanding,
                                                             Qt::SmoothTransformation));
    m_avatarLabel->setPixmap(pixmap);
}
