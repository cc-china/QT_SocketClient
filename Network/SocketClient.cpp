#include "SocketClient.h"
#include "Constant.h"
#include "MyDebug.h"
#include <QTimer>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define PACKET_SIZE  (1024 * 500)

SocketClient::SocketClient()
{
    m_dataSocket  = NULL;

    m_isConnecting = true;

    connect(&m_diagnoseTimer, SIGNAL(timeout()),
            this, SLOT(socketTimeOut()));
}

//使用单例模式
SocketClient *SocketClient::getInstance()
{
    static SocketClient instance; //局部静态变量，若定义为指针，则需要手动释放内容
    return &instance;
}

bool SocketClient::isConnected()
{
    MY_DEBUG("");
    if(m_dataSocket == NULL || (m_dataSocket !=  NULL &&
        m_dataSocket->state() !=  QTcpSocket::ConnectedState))
    {
        return false;
    }
    else
    {
        return true;
    }
}

void SocketClient::connectTcpServer(QHostAddress ip, int port)
{
    QTcpSocket *socket = new QTcpSocket(this);
    m_tcpServerIP = ip;
    m_tcpServerPort = port;
    socket->connectToHost(m_tcpServerIP, m_tcpServerPort);
    socket->waitForConnected();
    if(socket->state() == QTcpSocket::ConnectedState)
    {
        if(m_dataSocket != NULL)
        {
            m_dataSocket->abort();
            delete m_dataSocket;
        }

        m_dataSocket = socket;
        connect(m_dataSocket, SIGNAL(readyRead()), this, SLOT(OnReadyRead()));
        connect(m_dataSocket, SIGNAL(disconnected()), this, SLOT(OnDisconnected()));

        emit socketConnected(true);
    }
    else
    {
        delete socket;
        emit socketConnected(false);
    }
}

void SocketClient::OnReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if(socket == m_dataSocket )
    {
        m_dataPacket.data += socket->readAll();

        // 循环解析包数据，m_dataPacket.data 中可能不只一包数据
        bool isOk = false;
        do{
            isOk = parsePacket(socket, &m_dataPacket);
        }while(isOk);
    }
    else
    {
        MY_LOG("socket connection abnormal");
    }


}

// 解包
bool SocketClient::parsePacket(QTcpSocket *socket, SocketPacket *packet)
{
    int pIndexStart = packet->data.indexOf(NET_PACKET_START);
    if(pIndexStart < 0)
    {
        return false;
    }

    packet->data = packet->data.mid(pIndexStart); //截取从包头index_start到末尾的数据
    SocketPacket tmpPacket;
    tmpPacket.data = packet->data;

    tmpPacket.data.remove(0, QByteArray(NET_PACKET_START).size());//删除包头

    //解析包长度
    if(tmpPacket.data.count() < NET_PACKET_LTNGTH_BYTES)
    {
        return false;
    }
    bool isOk;
    tmpPacket.length = tmpPacket.data.mid(0, NET_PACKET_LTNGTH_BYTES).toLong(&isOk);
    if(isOk == false)
    {
        packet->data.remove(0, QByteArray(NET_PACKET_START).size());//删除包头
        if(packet->data.indexOf(NET_PACKET_START) >= 0)
        {
            return true;//有可能出现粘包的情况，继续解析后面数据
        }
        else
        {
            return false;
        }
    }

    //数据到达包长度
    tmpPacket.data.remove(0, NET_PACKET_LTNGTH_BYTES);//删除数据长度
    if(tmpPacket.length > tmpPacket.data.count())
    {
        return false;
    }

    //包尾是否匹配
    tmpPacket.data.resize(tmpPacket.length);//删除多余数据
    if(tmpPacket.data.endsWith(NET_PACKET_END) == false)
    {
        packet->data.remove(0, QByteArray(NET_PACKET_START).size());//删除包头
        if(packet->data.indexOf(NET_PACKET_START) >= 0)
        {
            return true;//有可能出现粘包的情况，继续解析后面数据
        }
        else
        {
            return false;
        }
    }

    tmpPacket.data.resize(tmpPacket.length -
                          QByteArray(NET_PACKET_END).count()); //删除包尾

    //解析出数据类型
    if(tmpPacket.data.count() < NET_PACKET_TYPE_BYTES)
    {
        return false;
    }
    QByteArray dataType = tmpPacket.data.left(NET_PACKET_TYPE_BYTES);
    tmpPacket.dataType = dataType;

    tmpPacket.data.remove(0, NET_PACKET_TYPE_BYTES);//删除数据类型


    //发送数据包消息
    if(socket == m_dataSocket)
    {
        emit dataPacketReady(tmpPacket);
    }

    //删除当前包数据
    packet->data.remove(0,
                        QByteArray(NET_PACKET_START).size()
                        + NET_PACKET_LTNGTH_BYTES
                        + tmpPacket.length);

    return true;
}

void SocketClient::OnDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if(m_dataSocket !=  NULL)
    {
        m_dataSocket->abort();
        m_dataSocket = NULL;
    }

    m_diagnoseTimer.stop();

    emit socketConnected(false);

    MY_DEBUG("");
}

// 封包并发送
void SocketClient::send(QByteArray data, QByteArray dataType)
{
    QTcpSocket *socket = m_dataSocket;

    if(socket == NULL || (socket !=  NULL &&
       socket->state() !=  QTcpSocket::ConnectedState))
    {
        return;
    }

    QByteArray packet = dataType + data + NET_PACKET_END; //[包类型 + 数据 + 包尾]

    //长度占8字节，前面补零，如"00065536"
    int size = packet.size();
    QByteArray length = QByteArray().setNum(size);
    length = QByteArray(NET_PACKET_LTNGTH_BYTES, '0') + length;
    length = length.right(NET_PACKET_LTNGTH_BYTES);

    packet.insert(0, NET_PACKET_START + length);//插入 [包头 + 数据长度]

    socket->write(packet);
}

void SocketClient::socketTimeOut()
{
    MY_DEBUG("socketTimeOut !");
    m_diagnoseTimer.stop();
    OnDisconnected();
}





