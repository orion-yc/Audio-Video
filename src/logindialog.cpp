#include "logindialog.h"
#include "ui_logindialog.h"

#include "packdef.h"

#include <QBuffer>
#include <QComboBox>
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>

namespace {
constexpr int kBuiltInAvatarCount = 36;
constexpr int kAvatarEdge = 256;
}

LoginDialog::LoginDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoginDialog),
    m_latencyLabel(new LatencyLabel(this)),
    m_registerNicknameLabel(nullptr),
    m_registerAvatarLabel(nullptr),
    m_registerNicknameEdit(nullptr),
    m_registerAvatarCombo(nullptr),
    m_registerAvatarUploadButton(nullptr),
    m_registerCustomAvatarIndex(-1),
    m_registerAvatarChanged(false),
    m_blockRegisterAvatarSelection(false)
{
    ui->setupUi(this);

    m_registerNicknameLabel = new QLabel("昵称（选填）", ui->page_2);
    m_registerAvatarLabel = new QLabel("头像（选填）", ui->page_2);
    m_registerNicknameEdit = new QLineEdit(ui->page_2);
    m_registerAvatarCombo = new QComboBox(ui->page_2);
    m_registerAvatarUploadButton = new QPushButton("上传头像", ui->page_2);

    setWindowTitle("Audio-Video");
    setFixedSize(820, 500);

    constexpr int sideWidth = 276;
    constexpr int panelWidth = 544;
    ui->lb_icon->setGeometry(0, 0, sideWidth, 500);
    ui->lb_icon->setPixmap(QPixmap());
    ui->lb_icon->setText("AUDIO-VIDEO");
    ui->lb_icon->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui->tw_page->setMinimumSize(panelWidth, 500);
    ui->tw_page->setMaximumSize(panelWidth, 500);
    ui->tw_page->setGeometry(sideWidth, 0, panelWidth, 500);
    ui->tw_page->setDocumentMode(true);

    const int labelX = 42;
    const int labelWidth = 92;
    const int inputX = 150;
    const int inputWidth = 336;
    const int inputHeight = 42;
    const int buttonY = 282;
    const int clearWidth = 126;

    ui->lb_tel->setGeometry(labelX, 128, labelWidth, inputHeight);
    ui->le_tel->setGeometry(inputX, 128, inputWidth, inputHeight);
    ui->lb_passwd->setGeometry(labelX, 188, labelWidth, inputHeight);
    ui->le_passwd->setGeometry(inputX, 188, inputWidth, inputHeight);
    ui->pb_clear->setGeometry(inputX, buttonY, clearWidth, inputHeight);
    ui->pb_commit->setGeometry(inputX + clearWidth + 12, buttonY,
                               inputWidth - clearWidth - 12, inputHeight);

    const int registerButtonY = 338;
    ui->lb_tel_register->setGeometry(labelX, 54, labelWidth, inputHeight);
    ui->le_tel_register->setGeometry(inputX, 54, inputWidth, inputHeight);
    ui->lb_passwd_register->setGeometry(labelX, 108, labelWidth, inputHeight);
    ui->le_passwd_register->setGeometry(inputX, 108, inputWidth, inputHeight);
    ui->lb_confirm_register->setGeometry(labelX, 162, labelWidth, inputHeight);
    ui->le_confirm_register->setGeometry(inputX, 162, inputWidth, inputHeight);
    ui->pb_clear_register->setGeometry(inputX, registerButtonY, clearWidth, inputHeight);
    ui->pb_commit_register->setGeometry(inputX + clearWidth + 12, registerButtonY,
                                        inputWidth - clearWidth - 12, inputHeight);

    m_registerNicknameLabel->setGeometry(labelX, 216, labelWidth, inputHeight);
    m_registerNicknameEdit->setGeometry(inputX, 216, inputWidth, inputHeight);
    m_registerAvatarLabel->setGeometry(labelX, 270, labelWidth, inputHeight);
    m_registerAvatarCombo->setGeometry(inputX, 270, 174, inputHeight);
    m_registerAvatarUploadButton->setGeometry(inputX + 186, 270,
                                               inputWidth - 186, inputHeight);
    m_registerNicknameLabel->setStyleSheet(ui->lb_tel_register->styleSheet());
    m_registerAvatarLabel->setStyleSheet(ui->lb_tel_register->styleSheet());
    m_registerAvatarCombo->setIconSize(QSize(30, 30));
    m_registerAvatarCombo->setToolTip("选择内置头像，或上传自定义头像");
    m_registerAvatarCombo->addItem("使用默认头像");
    for (int i = 0; i < kBuiltInAvatarCount; ++i) {
        m_registerAvatarCombo->addItem(QIcon(QString(":/tx/%1.png").arg(i)),
                                       QString("内置头像 %1").arg(i + 1));
    }

    ui->le_tel->setPlaceholderText("请输入手机号");
    ui->le_passwd->setPlaceholderText("请输入密码");
    ui->le_tel_register->setPlaceholderText("请输入手机号");
    ui->le_passwd_register->setPlaceholderText("请输入密码");
    ui->le_confirm_register->setPlaceholderText("请再次输入密码");
    m_registerNicknameEdit->setPlaceholderText("不填写则使用系统默认昵称");
    ui->pb_commit->setText("登录");
    ui->pb_commit_register->setText("注册");

    m_latencyLabel->move(18, 18);
    m_latencyLabel->raise();

    connect(m_registerAvatarUploadButton, &QPushButton::clicked,
            this, &LoginDialog::on_pb_uploadRegisterAvatar_clicked);
    connect(m_registerAvatarCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &LoginDialog::on_cbx_registerAvatar_currentIndexChanged);

    on_pb_clear_clicked();
    on_pb_clear_register_clicked();
}

void LoginDialog::slot_setLatency(int latencyMs, const QString &source)
{
    m_latencyLabel->setLatency(latencyMs, source);
}

void LoginDialog::slot_setLoginPending(bool pending)
{
    ui->pb_commit->setEnabled(!pending);
    ui->pb_commit->setText(pending ? "登录中..." : "登录");
}

void LoginDialog::slot_showLoginFor(const QString &tel)
{
    ui->tw_page->setCurrentIndex(0);
    ui->le_tel->setText(tel);
    ui->le_passwd->clear();
    ui->le_passwd->setFocus();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::closeEvent(QCloseEvent *event)
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

#include <QMessageBox>
#include <QRegularExpression>
// 登录清空
void LoginDialog::on_pb_clear_clicked()
{
    ui->le_tel->setText("");
    ui->le_passwd->setText("");
}

// 登录提交
void LoginDialog::on_pb_commit_clicked()
{
    QString strTel = ui->le_tel->text();
    QString strPasswd = ui->le_passwd->text();

    // 合法化过滤
    // 非空   isEmpty ""
    if(strTel.isEmpty() || strPasswd.isEmpty() ){
        QMessageBox::about(this, "提示", "输入不能为空");
        return ;
    }
    // 手机号验证    1[3-8][0-9] 9位
    QRegularExpression reg("^1[3-8][0-9]{6,9}$");
    bool res = reg.match(strTel).hasMatch();
    if(!res){
        QMessageBox::about(this, "提示", "输入的手机号非法，需要8-11位手机号");
        return ;
    }
    // 密码合法化    长度不超过20
    if(strPasswd.length() > 20){
        QMessageBox::about(this, "提示", "输入密码非法,长度不超过20");
        return ;
    }

    // 得到最终的 tel passwd

    // 发送信号
    Q_EMIT SIG_loginCommit(strTel, strPasswd);
}

// 注册清空
void LoginDialog::on_pb_clear_register_clicked()
{
    ui->le_tel_register->setText("");
    ui->le_passwd_register->setText("");
    ui->le_confirm_register->setText("");
    resetRegisterProfile();
}


// 注册提交
void LoginDialog::on_pb_commit_register_clicked()
{
    QString strTel = ui->le_tel_register->text();
    QString strPasswd = ui->le_passwd_register->text();
    QString strConfirm = ui->le_confirm_register->text();
    const QString nickname = m_registerNicknameEdit->text().trimmed();
    // 合法化过滤
    // 非空   isEmpty ""
    if(strTel.isEmpty() || strPasswd.isEmpty() || strConfirm.isEmpty()){
        QMessageBox::about(this, "提示", "输入不能为空");
        return ;
    }
    // 手机号验证    1[3-8][0-9] 9位
    QRegularExpression reg("^1[3-8][0-9]{6,9}$");
    bool res = reg.match(strTel).hasMatch();
    if(!res){
        QMessageBox::about(this, "提示", "输入的手机号非法，需要8-11位手机号");
        return ;
    }
    // 密码合法化    长度不超过20
    if(strPasswd.length() > 20){
        QMessageBox::about(this, "提示", "输入密码非法,长度不超过20");
        return ;
    }
    // 再次确认     要一致
    if(strPasswd != strConfirm){
        QMessageBox::about(this, "提示", "两次密码不一致");
        return ;
    }
    if (nickname.length() > 20) {
        QMessageBox::about(this, "提示", "昵称长度不能超过20个字符");
        return;
    }

    int iconid = 0;
    const int avatarIndex = m_registerAvatarCombo->currentIndex();
    if (avatarIndex > 0 && avatarIndex != m_registerCustomAvatarIndex) {
        iconid = avatarIndex - 1;
    }

    Q_EMIT SIG_registerCommit(strTel, strPasswd, nickname, iconid,
                              m_registerAvatarData, m_registerAvatarChanged);
}

QByteArray LoginDialog::encodeRegisterAvatar(const QImage &avatar) const
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

void LoginDialog::setRegisterCustomAvatar(const QImage &avatar)
{
    const QByteArray data = encodeRegisterAvatar(avatar);
    if (data.isEmpty()) {
        QMessageBox::warning(this, "提示", "头像处理失败，请选择 JPG、PNG 或 BMP 图片");
        return;
    }

    m_blockRegisterAvatarSelection = true;
    const QIcon icon(QPixmap::fromImage(avatar.scaled(30, 30, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation)));
    if (m_registerCustomAvatarIndex < 0) {
        m_registerCustomAvatarIndex = m_registerAvatarCombo->count();
        m_registerAvatarCombo->addItem(icon, "已上传头像");
    } else {
        m_registerAvatarCombo->setItemIcon(m_registerCustomAvatarIndex, icon);
        m_registerAvatarCombo->setItemText(m_registerCustomAvatarIndex, "已上传头像");
    }
    m_registerAvatarCombo->setCurrentIndex(m_registerCustomAvatarIndex);
    m_blockRegisterAvatarSelection = false;

    m_registerAvatarData = data;
    m_registerAvatarChanged = true;
}

void LoginDialog::resetRegisterProfile()
{
    m_registerNicknameEdit->clear();
    m_blockRegisterAvatarSelection = true;
    if (m_registerCustomAvatarIndex >= 0) {
        m_registerAvatarCombo->removeItem(m_registerCustomAvatarIndex);
        m_registerCustomAvatarIndex = -1;
    }
    m_registerAvatarCombo->setCurrentIndex(0);
    m_blockRegisterAvatarSelection = false;
    m_registerAvatarData.clear();
    m_registerAvatarChanged = false;
}

void LoginDialog::on_pb_uploadRegisterAvatar_clicked()
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
    setRegisterCustomAvatar(avatar);
}

void LoginDialog::on_cbx_registerAvatar_currentIndexChanged(int index)
{
    if (m_blockRegisterAvatarSelection) {
        return;
    }

    if (index != m_registerCustomAvatarIndex && m_registerCustomAvatarIndex >= 0) {
        m_blockRegisterAvatarSelection = true;
        m_registerAvatarCombo->removeItem(m_registerCustomAvatarIndex);
        m_registerCustomAvatarIndex = -1;
        m_blockRegisterAvatarSelection = false;
    }

    if (index == 0) {
        m_registerAvatarData.clear();
        m_registerAvatarChanged = false;
    } else {
        m_registerAvatarData.clear();
        m_registerAvatarChanged = true;
    }
}

