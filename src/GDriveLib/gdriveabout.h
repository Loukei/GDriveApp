#ifndef GDRIVEABOUT_H
#define GDRIVEABOUT_H
#include <QObject>
#include <QNetworkReply> //for network reply error
#include "gdriveaboutresource.h"

QT_BEGIN_NAMESPACE
class QOAuth2AuthorizationCodeFlow;
QT_END_NAMESPACE

namespace GDrive {
/*!
 * \class GDriveAbout
 * \brief Gets information about the user, the user's Drive, and system capabilities
 *
 * - GDriveAbout 負責發送並保存使用者的About資訊
 * - 實作Google Drive Api的`About: get`方法
 * - 發送網路請求的工作由`QOAuth2AuthorizationCodeFlow`物件處理，發送過程在constructor中進行，
 * 而產生的`QNetworkReply`實際上也是由`QOAuth2AuthorizationCodeFlow`持有。
 * - 基於上一點的原因，不可以在`GDriveAbout`工作完成前刪除`QOAuth2AuthorizationCodeFlow`物件，
 * 否則會產生不可預期的錯誤
 * - 發出`GDriveAbout::finished()` 信號表示已接收到網路回應
 * - 使用`GDriveAbout::isComplete()`表示任務完成與否
 * - 使用`GDriveAbout::isFailed()`表示任務失敗與否
 * - 使用 `QObject::deleteLater()`刪除，而非 delete
 * - 透過`QByteArray getReplyString()`取出字串版的json資料
 * - 透過`GDriveAboutResource getResource()`取出`GDriveAboutResource`類型的資料
 * - AboutArg是用來快速指定想要的回傳範圍，一般人只在意使用者名稱所以便沒有實作更多參數。
 * 但是你可以透過`fields`參數自訂內容
 *
 * ## Reference
 * - [About: get](https://developers.google.com/drive/api/v3/reference/about/get)
 */
class GDriveAbout : public QObject
{
    Q_OBJECT
public:
    /// Constructor,use direct url to send request
    explicit GDriveAbout(QOAuth2AuthorizationCodeFlow *parent,const QString &fields);
    /// Destructor
    virtual ~GDriveAbout() final;
    /// return About resource to JSON string data format
    QByteArray getReplyString() const;
    /// return true if task complete
    bool isComplete() const;
    /// return true if error occured
    bool isFailed() const;

signals:
    /// emit when error occured or task complete
    void finished();

protected:
    const QString key_AboutUrl = "https://www.googleapis.com/drive/v3/about";
    /// Turn fields to request Url
    QUrl buildRequestUrl(const QString &field) const;

private:
    /// save reply data
    QByteArray m_data = QByteArray();
    /// Does task complete and not failed?
    bool m_isFailed = false;
    /// Does task complete
    bool m_isComplete = false;

private slots:
    /// process network reply finish
    void onReplyFinished();
    /// process network reply error
    void onReplyError(QNetworkReply::NetworkError);
};
}
#endif // GDRIVEABOUT_H
