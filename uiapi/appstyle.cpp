#include "appstyle.h"

#include <QApplication>
#include <QColor>
#include <QPalette>

void applyAppStyle(QApplication &application)
{
    QPalette palette = application.palette();
    palette.setColor(QPalette::Window, QColor("#111315"));
    palette.setColor(QPalette::WindowText, QColor("#F4F4F5"));
    palette.setColor(QPalette::Base, QColor("#171A1E"));
    palette.setColor(QPalette::AlternateBase, QColor("#20242A"));
    palette.setColor(QPalette::Text, QColor("#F4F4F5"));
    palette.setColor(QPalette::Button, QColor("#20242A"));
    palette.setColor(QPalette::ButtonText, QColor("#F4F4F5"));
    palette.setColor(QPalette::Highlight, QColor("#E5484D"));
    palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    application.setPalette(palette);

    application.setStyleSheet(QStringLiteral(R"qss(
        QWidget {
            color: #F4F4F5;
            font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
            font-size: 13px;
        }

        QLabel { background: transparent; }
        QDialog { background: #111315; }

        QDialog#LoginDialog {
            background: #111315;
        }

        QLabel#lb_icon {
            background: #08090B;
            border-right: 1px solid #3B1216;
            border-left: 4px solid #E5484D;
            color: #F4F4F5;
            font-size: 30px;
            font-weight: 700;
            letter-spacing: 2px;
            padding: 28px 24px;
        }

        QTabWidget#tw_page::pane {
            background: #111315;
            border: 0;
        }

        QTabWidget#tw_page QTabBar::tab {
            background: transparent;
            border: 0;
            border-bottom: 2px solid transparent;
            color: #8D949E;
            min-width: 112px;
            min-height: 44px;
            padding: 0 18px;
        }

        QTabWidget#tw_page QTabBar::tab:hover {
            color: #F4F4F5;
        }

        QTabWidget#tw_page QTabBar::tab:selected {
            border-bottom-color: #E5484D;
            color: #FFFFFF;
            font-weight: 600;
        }

        QLineEdit {
            background: #181B20;
            border: 1px solid #343941;
            border-radius: 6px;
            color: #F8FAFC;
            min-height: 38px;
            padding: 0 12px;
            selection-background-color: #B4232A;
        }

        QLineEdit:hover { border-color: #575F6B; }

        QLineEdit:focus {
            background: #1C2025;
            border: 1px solid #E5484D;
        }

        QLineEdit:disabled {
            background: #141619;
            color: #6F7680;
        }

        QLineEdit::placeholder { color: #777F8B; }

        QPushButton {
            background: #24282E;
            border: 1px solid #3C424B;
            border-radius: 6px;
            color: #F4F4F5;
            min-height: 38px;
            padding: 0 16px;
        }

        QPushButton:hover {
            background: #30353C;
            border-color: #626B76;
        }

        QPushButton:pressed { background: #171A1E; }
        QPushButton:disabled { background: #1A1C20; border-color: #2A2E34; color: #6F7680; }

        QPushButton#pb_commit,
        QPushButton#pb_commit_register,
        QPushButton#pb_quit {
            background: #C9363D;
            border: 1px solid #E5484D;
            color: #FFFFFF;
            font-weight: 600;
        }

        QPushButton#pb_commit:hover,
        QPushButton#pb_commit_register:hover,
        QPushButton#pb_quit:hover {
            background: #E5484D;
            border-color: #FF6970;
        }

        QPushButton#pb_min,
        QPushButton#pb_max,
        QPushButton#pb_hide,
        QPushButton#pb_close {
            background: transparent;
            border: 0;
            border-radius: 4px;
            color: #A5ACB5;
            min-height: 30px;
            min-width: 30px;
            padding: 0;
        }

        QPushButton#pb_min:hover,
        QPushButton#pb_max:hover,
        QPushButton#pb_hide:hover {
            background: #2A2E34;
            color: #FFFFFF;
        }

        QPushButton#pb_close:hover {
            background: #C9363D;
            color: #FFFFFF;
        }

        QDialog#demoDialog,
        QWidget#wdg_content {
            background: #111315;
        }

        QWidget#wdg_title,
        QWidget#wdg_top {
            background: #0B0C0E;
            border-bottom: 1px solid #2D1518;
        }

        QLabel#lb_title {
            color: #FFFFFF;
            font-size: 14px;
            font-weight: 600;
        }

        QLabel#lb_title_icon { border-radius: 4px; }

        QWidget#wdg_user {
            background: #16191D;
            border-bottom: 1px solid #292D33;
        }

        QLabel#lb_name { color: #F4F4F5; font-weight: 600; }

        QPushButton#pb_icon {
            background: #0D0F12;
            border: 1px solid #4A1A1E;
            border-radius: 24px;
            min-width: 48px;
            max-width: 48px;
            min-height: 48px;
            max-height: 48px;
            padding: 0;
        }

        QPushButton#pb_icon:hover { border-color: #E5484D; }

        QPushButton#pb_setup {
            background: #1A1D22;
            border: 1px solid #3C424B;
            border-radius: 4px;
            color: #DCE1E8;
            font-weight: 600;
            min-width: 52px;
            min-height: 32px;
            padding: 0 8px;
        }

        QPushButton#pb_setup:hover {
            background: #2A171A;
            border-color: #E5484D;
            color: #FFFFFF;
        }

        QWidget#wdg_room,
        QWidget#wdg_bottom { background: #111315; }

        QToolButton#pb_join,
        QToolButton#pb_create,
        QToolButton#pb_history {
            background: #1A1D22;
            border: 1px solid #333840;
            border-radius: 6px;
            color: #E6E8EB;
            font-weight: 600;
            padding: 10px;
        }

        QToolButton#pb_join:hover,
        QToolButton#pb_create:hover {
            background: #2A171A;
            border-color: #E5484D;
        }

        QToolButton#pb_join:pressed,
        QToolButton#pb_create:pressed { background: #47171B; }
        QToolButton#pb_history:disabled { color: #666D77; border-color: #292D33; }

        QDialog#RoomDialog,
        QWidget#wdg_mid {
            background: #0A0B0D;
        }

        QScrollArea#sa_userList {
            background: #101216;
            border: 1px solid #292D33;
            border-radius: 6px;
        }

        QScrollArea#sa_userList QWidget#scrollAreaWidgetContents,
        QWidget#wdg_userList { background: #101216; }

        QFrame#wdg_chatPanel {
            background: #101216;
            border: 1px solid #292D33;
            border-radius: 6px;
        }

        QLabel#lb_chatTitle { color: #FFFFFF; font-weight: 600; }
        QLabel#lb_chatHint { color: #777F8B; font-size: 12px; }

        QTextBrowser#tb_chatHistory {
            background: #0D0F12;
            border: 1px solid #252A30;
            border-radius: 4px;
            padding: 8px;
        }

        QTextEdit#te_chatInput {
            background: #181B20;
            border: 1px solid #343941;
            border-radius: 4px;
            color: #F8FAFC;
            padding: 7px 9px;
        }

        QTextEdit#te_chatInput:focus { border-color: #E5484D; }

        QPushButton#pb_chatToggle,
        QPushButton#pb_chatSend {
            background: #24282E;
            border: 1px solid #3C424B;
            color: #F4F4F5;
            font-weight: 600;
            padding: 0 10px;
        }

        QPushButton#pb_chatToggle:hover,
        QPushButton#pb_chatSend:hover {
            background: #2A171A;
            border-color: #E5484D;
            color: #FFFFFF;
        }

        QToolButton#pb_chatClose {
            background: transparent;
            border: 0;
            color: #A5ACB5;
            min-width: 24px;
            min-height: 24px;
        }

        QToolButton#pb_chatClose:hover { background: #2A2E34; color: #FFFFFF; }

        QDialog#UserProfileDialog { background: #111315; }

        QLabel#lb_profileAvatar {
            background: #090A0C;
            border: 2px solid #C9363D;
            border-radius: 6px;
        }

        QLabel#lb_profileName {
            color: #FFFFFF;
            font-size: 17px;
            font-weight: 600;
        }

        QLabel#lb_profileMemberType { color: #FF8B90; font-size: 12px; }

        QFrame#wdg_profileDetails {
            background: #0D0F12;
            border: 1px solid #292D33;
            border-radius: 6px;
        }

        QLabel#lb_profileCaption { color: #777F8B; font-size: 12px; }
        QLabel#lb_profileValue { color: #F4F4F5; font-weight: 600; }
        QLabel#lb_profileFeeling { color: #D7DBE0; }

        QPushButton#pb_profileClose {
            background: #24282E;
            border: 1px solid #3C424B;
            color: #F4F4F5;
            font-weight: 600;
            padding: 0 10px;
        }

        QPushButton#pb_profileClose:hover {
            background: #2A171A;
            border-color: #E5484D;
            color: #FFFFFF;
        }

        UserShow {
            background: #090A0C;
            border: 1px solid #2D1518;
            border-radius: 6px;
        }

        UserShow QLabel#lb_name {
            background: #16191D;
            border-bottom: 1px solid #2D1518;
            color: #F4F4F5;
            font-size: 12px;
            font-weight: 600;
        }

        QCheckBox {
            color: #BEC4CC;
            min-height: 30px;
            spacing: 7px;
        }

        QCheckBox:hover { color: #FFFFFF; }

        QCheckBox::indicator {
            background: #16191D;
            border: 1px solid #4B535E;
            border-radius: 4px;
            height: 16px;
            width: 16px;
        }

        QCheckBox::indicator:hover { border-color: #E5484D; }
        QCheckBox::indicator:checked { background: #C9363D; border-color: #E5484D; }

        QComboBox {
            background: #181B20;
            border: 1px solid #343941;
            border-radius: 6px;
            color: #F4F4F5;
            min-height: 30px;
            padding: 0 10px;
        }

        QComboBox:hover { border-color: #575F6B; }
        QComboBox::drop-down { border: 0; width: 26px; }
        QComboBox QAbstractItemView { background: #1A1D22; border: 1px solid #3C424B; selection-background-color: #C9363D; }

        QScrollBar:vertical {
            background: transparent;
            margin: 4px 2px;
            width: 8px;
        }

        QScrollBar::handle:vertical { background: #3A4049; border-radius: 4px; min-height: 28px; }
        QScrollBar::handle:vertical:hover { background: #575F6B; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

        QMessageBox { background: #181B20; }
        QMessageBox QLabel { color: #F4F4F5; }
        QToolTip { background: #20242A; border: 1px solid #4B535E; color: #FFFFFF; padding: 5px; }
    )qss"));
}
