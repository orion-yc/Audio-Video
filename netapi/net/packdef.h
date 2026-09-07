#pragma once

#include <cstddef>


#define _DEF_BUFFER  ( 4096 )
#define _DEF_CONTENT_SIZE	(1024)
#define _MAX_SIZE	(40)
#define _DEF_MAX_PACKET_SIZE (8 * 1024 * 1024)
#define SESSION_TOKEN_SIZE (33)

//自定义协议   先写协议头 再写协议结构
//登录 注册 获取好友信息 添加好友 聊天 发文件 下线请求
#define DEF_PACK_BASE	(10000)
#define DEF_PACK_COUNT (100)

//注册
#define  DEF_PACK_REGISTER_RQ    (DEF_PACK_BASE + 0)
#define  DEF_PACK_REGISTER_RS    (DEF_PACK_BASE + 1)
//登录
#define  DEF_PACK_LOGIN_RQ    (DEF_PACK_BASE + 2)
#define  DEF_PACK_LOGIN_RS    (DEF_PACK_BASE + 3)
//创建房间
#define DEF_PACK_CREATEROOM_RQ  (DEF_PACK_BASE + 4)
#define DEF_PACK_CREATEROOM_RS  (DEF_PACK_BASE + 5)
//加入房间
#define DEF_PACK_JOINROOM_RQ  (DEF_PACK_BASE + 6)
#define DEF_PACK_JOINROOM_RS  (DEF_PACK_BASE + 7)
//房间列表请求
#define DEF_PACK_ROOM_MEMBER    (DEF_PACK_BASE + 8)
//音频数据
#define DEF_PACK_AUDIO_FRAME    (DEF_PACK_BASE + 9)
//视频数据
#define DEF_PACK_VIDEO_FRAME    (DEF_PACK_BASE + 10)
//退出房间请求
#define DEF_PACK_LEAVEROOM_RQ   (DEF_PACK_BASE + 11)
//退出房间回复
#define DEF_PACK_LEAVEROOM_RS   (DEF_PACK_BASE + 12)
//音频注册
#define DEF_PACK_AUDIO_REGISTER (DEF_PACK_BASE + 13)
//视频注册
#define DEF_PACK_VIDEO_REGISTER (DEF_PACK_BASE + 14)
//用户信息 用于设置和登录时获取状态
#define DEF_PACK_USER_INFO_RQ   (DEF_PACK_BASE + 15)
//自定义头像上传、查询与下发
#define DEF_PACK_AVATAR_UPLOAD_RQ (DEF_PACK_BASE + 16)
#define DEF_PACK_AVATAR_UPLOAD_RS (DEF_PACK_BASE + 17)
#define DEF_PACK_AVATAR_FETCH_RQ  (DEF_PACK_BASE + 18)
#define DEF_PACK_AVATAR_DATA_RS   (DEF_PACK_BASE + 19)
// 房间公共文字聊天，使用已认证的主控制连接。
#define DEF_PACK_ROOM_CHAT_MESSAGE (DEF_PACK_BASE + 20)

#define DEF_AVATAR_IMAGE_MAX_SIZE (256 * 1024)
#define DEF_ROOM_CHAT_MAX_BYTES 512


//注册请求结果
#define tel_is_exist		(0)
#define register_success	(1)
#define name_is_exist       (2)

//登录请求结果
#define user_not_exist		(0)
#define password_error		(1)
#define login_success		(2)
#define server_error        (3)
//#define user_online          3

//创建房间结果
#define room_is_exist        0
#define create_success       1

//加入房间结果
#define room_no_exist        0
#define join_success         1


typedef int PackType;

//协议结构
//注册
typedef struct STRU_REGISTER_RQ
{
    STRU_REGISTER_RQ():type(DEF_PACK_REGISTER_RQ)
	{
		memset( tel  , 0, sizeof(tel));
        // memset( name  , 0, sizeof(name));
		memset( password , 0, sizeof(password) );
	}
	//需要手机号码 , 密码, 昵称
	PackType type;
	char tel[_MAX_SIZE];
    // char name[_MAX_SIZE];
	char password[_MAX_SIZE];

}STRU_REGISTER_RQ;

typedef struct STRU_REGISTER_RS
{
	//回复结果
    STRU_REGISTER_RS(): type(DEF_PACK_REGISTER_RS) , result(register_success)
	{
	}
	PackType type;
	int result;

}STRU_REGISTER_RS;

//登录
typedef struct STRU_LOGIN_RQ
{
	//登录需要: 手机号 密码 
    STRU_LOGIN_RQ():type(DEF_PACK_LOGIN_RQ)
	{
		memset( tel , 0, sizeof(tel) );
		memset( password , 0, sizeof(password) );
	}
	PackType type;
	char tel[_MAX_SIZE];
	char password[_MAX_SIZE];

}STRU_LOGIN_RQ;

typedef struct STRU_LOGIN_RS
{
	// 需要 结果 , 用户的id
    STRU_LOGIN_RS(): type(DEF_PACK_LOGIN_RS), userid(0), result(login_success)
	{
		memset(sessionToken, 0, sizeof(sessionToken));
	}
	PackType type;
    int userid;
	int result;
	char sessionToken[SESSION_TOKEN_SIZE];
}STRU_LOGIN_RS;

// Login packets use the compiler's default alignment. Do not pack them.
static_assert(sizeof(STRU_LOGIN_RQ) == 84,
              "Login request protocol layout must remain 84 bytes.");
static_assert(sizeof(STRU_LOGIN_RS) == 48,
              "Login response protocol layout must remain 48 bytes.");
static_assert(offsetof(STRU_LOGIN_RQ, type) == 0 &&
              offsetof(STRU_LOGIN_RQ, tel) == 4 &&
              offsetof(STRU_LOGIN_RQ, password) == 44,
              "Login request field offsets must remain stable.");
static_assert(offsetof(STRU_LOGIN_RS, type) == 0 &&
              offsetof(STRU_LOGIN_RS, userid) == 4 &&
              offsetof(STRU_LOGIN_RS, result) == 8 &&
              offsetof(STRU_LOGIN_RS, sessionToken) == 12,
              "Login response field offsets must remain stable.");

////用户信息
//typedef struct STRU_USER_INFO
//{
//    STRU_USER_INFO():type(DEF_PACK_USER_INFO) , userid(0) , iconid(0)
//	{
//		memset( name ,  0 , sizeof(name) );
//	}
//	//需要 用户id 头像id 昵称 签名 状态--是否在线
//	PackType type;
//	int userid;
//	int iconid;
//	char name[_MAX_SIZE];

//}STRU_USER_INFO;

//创建房间请求
typedef struct STRU_CREATEROOM_RQ
{
    STRU_CREATEROOM_RQ()
    {
        m_nType = DEF_PACK_CREATEROOM_RQ;
        m_UserID = 0;
    }

    PackType m_nType;   //包类型
    int m_UserID;

}STRU_CREATEROOM_RQ;

//创建房间回复
typedef struct STRU_CREATEROOM_RS
{
    STRU_CREATEROOM_RS()
    {
        m_nType= DEF_PACK_CREATEROOM_RS;
        m_lResult = 0;
        m_RoomId = 0;
    }
    PackType m_nType;   //包类型
    int  m_lResult ;    //创建结果
    int  m_RoomId;

}STRU_CREATEROOM_RS;

//加入房间请求
typedef struct STRU_JOINROOM_RQ
{
    STRU_JOINROOM_RQ()
    {
        m_nType = DEF_PACK_JOINROOM_RQ;
        m_UserID = 0;
        m_RoomID = 0;
    }

    PackType m_nType;   //包类型
    int m_UserID;
    int m_RoomID;

}STRU_JOINROOM_RQ;

//加入房间回复
typedef struct STRU_JOINROOM_RS
{
    STRU_JOINROOM_RS()
    {
        m_nType= DEF_PACK_JOINROOM_RS;
        m_lResult = 0;
        m_RoomID = 0;
    }
    PackType m_nType;   //包类型
    int  m_lResult ;    //注册结果
    int m_RoomID;
}STRU_JOINROOM_RS;

//房间成员请求
typedef struct STRU_ROOM_MEMBER_RQ
{
    STRU_ROOM_MEMBER_RQ()
    {
        m_nType= DEF_PACK_ROOM_MEMBER;
        m_UserID =0;
        memset(m_szUser,0,sizeof(m_szUser));
    }
    PackType m_nType;   //包类型
    int m_UserID;
    char m_szUser[_MAX_SIZE];

}STRU_ROOM_MEMBER_RQ;


//离开房间请求
typedef struct STRU_LEAVEROOM_RQ
{
    STRU_LEAVEROOM_RQ()
    {
        m_nType = DEF_PACK_LEAVEROOM_RQ;
        m_nUserId = 0;
        m_RoomId = 0;
        memset(szUserName,0,_MAX_SIZE);
    }
    PackType   m_nType;   //包类型
    int    m_nUserId; //用户ID
    int    m_RoomId;
    char   szUserName[_MAX_SIZE];
}STRU_LEAVEROOM_RQ;

//用户信息请求    登录之后的状态同步，设置用户信息
typedef struct STRU_USER_INFO_RQ
{
    STRU_USER_INFO_RQ()
    {
        m_nType= DEF_PACK_USER_INFO_RQ;
        m_UserID =0;    // 设置服务器写表，需要知道是谁
        m_iconid =0;
        memset(m_szUser,0,sizeof(m_szUser));
        memset(m_userFeeling,0,sizeof(m_userFeeling));
    }
    PackType m_nType;   //包类型
    int m_UserID;
    int m_iconid;
    char m_szUser[_MAX_SIZE];
    char m_userFeeling[_MAX_SIZE];

}STRU_USER_INFO_RQ;

// 自定义头像上传。图片内容紧跟固定头部，使用客户端压缩后的 JPEG 数据。
typedef struct STRU_AVATAR_UPLOAD_RQ
{
    STRU_AVATAR_UPLOAD_RQ()
        : m_nType(DEF_PACK_AVATAR_UPLOAD_RQ), m_UserID(0), m_imageSize(0)
    {
        memset(m_sessionToken, 0, sizeof(m_sessionToken));
    }
    PackType m_nType;
    int m_UserID;
    char m_sessionToken[SESSION_TOKEN_SIZE];
    int m_imageSize;
} STRU_AVATAR_UPLOAD_RQ;

typedef struct STRU_AVATAR_UPLOAD_RS
{
    STRU_AVATAR_UPLOAD_RS()
        : m_nType(DEF_PACK_AVATAR_UPLOAD_RS), m_UserID(0), m_result(0)
    {
    }
    PackType m_nType;
    int m_UserID;
    int m_result;
} STRU_AVATAR_UPLOAD_RS;

// 请求目标用户的自定义头像。服务端用请求者的会话令牌进行鉴权。
typedef struct STRU_AVATAR_FETCH_RQ
{
    STRU_AVATAR_FETCH_RQ()
        : m_nType(DEF_PACK_AVATAR_FETCH_RQ), m_RequestUserID(0), m_TargetUserID(0)
    {
        memset(m_sessionToken, 0, sizeof(m_sessionToken));
    }
    PackType m_nType;
    int m_RequestUserID;
    int m_TargetUserID;
    char m_sessionToken[SESSION_TOKEN_SIZE];
} STRU_AVATAR_FETCH_RQ;

// 头像数据包。图片内容紧跟固定头部；imageSize 为 0 表示用户使用内置头像。
typedef struct STRU_AVATAR_DATA_RS
{
    STRU_AVATAR_DATA_RS()
        : m_nType(DEF_PACK_AVATAR_DATA_RS), m_TargetUserID(0), m_result(0), m_imageSize(0)
    {
    }
    PackType m_nType;
    int m_TargetUserID;
    int m_result;
    int m_imageSize;
} STRU_AVATAR_DATA_RS;

static_assert(sizeof(STRU_AVATAR_UPLOAD_RQ) == 48,
              "Avatar upload protocol layout must remain 48 bytes.");
static_assert(sizeof(STRU_AVATAR_FETCH_RQ) == 48,
              "Avatar fetch protocol layout must remain 48 bytes.");
static_assert(sizeof(STRU_AVATAR_DATA_RS) == 16,
              "Avatar response protocol layout must remain 16 bytes.");

// 房间聊天请求和下发使用同一结构。messageLength 指示 m_message 中 UTF-8
// 文本的实际字节数，消息不携带结尾 '\0'。
typedef struct STRU_ROOM_CHAT_MESSAGE
{
    STRU_ROOM_CHAT_MESSAGE()
        : m_nType(DEF_PACK_ROOM_CHAT_MESSAGE), m_RoomID(0), m_UserID(0),
          m_messageLength(0)
    {
        memset(m_message, 0, sizeof(m_message));
    }
    PackType m_nType;
    int m_RoomID;
    int m_UserID;
    int m_messageLength;
    char m_message[DEF_ROOM_CHAT_MAX_BYTES];
} STRU_ROOM_CHAT_MESSAGE;

static_assert(sizeof(STRU_ROOM_CHAT_MESSAGE) == 528,
              "Room chat protocol layout must remain 528 bytes.");

//注册音频
struct STRU_AUDIO_REGISTER
{
    STRU_AUDIO_REGISTER():m_nType(DEF_PACK_AUDIO_REGISTER){
        m_userid = 0;
        memset(m_sessionToken, 0, sizeof(m_sessionToken));
    }

    PackType m_nType;   //包类型
    int m_userid;
    char m_sessionToken[SESSION_TOKEN_SIZE];
};

//注册视频
struct STRU_VIDEO_REGISTER
{
    STRU_VIDEO_REGISTER():m_nType(DEF_PACK_VIDEO_REGISTER){
        m_userid = 0;
        memset(m_sessionToken, 0, sizeof(m_sessionToken));
    }

    PackType m_nType;   //包类型
    int m_userid;
    char m_sessionToken[SESSION_TOKEN_SIZE];
};

static_assert(sizeof(STRU_AUDIO_REGISTER) == 44,
              "Audio registration protocol layout must remain 44 bytes.");
static_assert(sizeof(STRU_VIDEO_REGISTER) == 44,
              "Video registration protocol layout must remain 44 bytes.");

// 数据库
/*
	表1 用户表 id->唯一标识 tel  password  name  iconid   feeling 也可以写成两张表 登录和用户信息
	列:  id  tel password  name  iconid  feeling

	表2 好友  idA  idB
	列 idA  idB  
	操作: 好友关系是双向关系  关注是单向关系  , 添加好友时 , 需要写入两行 即 A->B B->A 方便查询
	(添加好友 查找一般比插入多)

//	表3 离线消息
	
	用户数据表t_user
	create table t_user ( id bigint unsigned  AUTO_INCREMENT primary key, tel varchar (260) , 
	password varchar(260)  , name varchar (260) , icon int , feeling varchar(260));

	可以添加测试数据 
	insert into t_user ( tel,password , name , icon , feeling  ) 
	values ('12345','test', '12345' ,1,'比较懒,什么也没写')

	好友表 t_friend
	create table t_friend ( idA bigint unsigned  , idB bigint unsigned);

*/

///音频数据帧
/// 成员描述
/// int type;
/// int userId;
/// int roomId;
/// int hour;
/// int min;
/// int sec;
/// int msec;
/// QByteArray audioFrame;  --> char frame[]; 柔性数组
///


///视频数据帧
/// 成员描述
/// int type;
/// int userId;
/// int roomId;
/// int hour;
/// int min;
/// int sec;
/// int msec;
/// QByteArray videoFrame;
///









// Media registration acknowledgement; success is zero. Echo the session token
// so a delayed response from an earlier login cannot authenticate a channel.
#define DEF_PACK_AUDIO_REGISTER_RS (DEF_PACK_BASE + 21)
#define DEF_PACK_VIDEO_REGISTER_RS (DEF_PACK_BASE + 22)
struct STRU_MEDIA_REGISTER_RS {
    PackType type;
    int userId;
    int result;
    char sessionToken[SESSION_TOKEN_SIZE];
};
static_assert(sizeof(STRU_MEDIA_REGISTER_RS) == 48, "media acknowledgement layout");

