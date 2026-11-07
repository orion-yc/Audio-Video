#ifndef USERPROFILEDIALOG_H
#define USERPROFILEDIALOG_H

#include <QDialog>
#include <QImage>

class QLabel;

class UserProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserProfileDialog(QWidget *parent = nullptr);

    void setProfile(int userId, const QString &name, const QImage &avatar,
                    const QString &feeling, bool currentUser);
    void slot_setAvatar(const QImage &avatar);
    int userId() const;

private:
    void updateAvatar(const QImage &avatar);

    int m_userId = 0;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_memberTypeLabel = nullptr;
    QLabel *m_userIdValueLabel = nullptr;
    QLabel *m_feelingValueLabel = nullptr;
};

#endif // USERPROFILEDIALOG_H
