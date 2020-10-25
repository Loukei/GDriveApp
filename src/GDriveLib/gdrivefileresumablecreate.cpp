#include "gdrivefileresumablecreate.h"
#include <QOAuth2AuthorizationCodeFlow>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrlQuery>
#include <QRegularExpression>

GDrive::GDriveFileResumableCreate::GDriveFileResumableCreate(const QString &filepath,
                                                             QOAuth2AuthorizationCodeFlow *parent)
    :GDriveFileTask (parent),m_file(filepath,this),m_status(Initial)
{
    m_initialUrl = buildUrl("resumable");
}

GDrive::GDriveFileResumableCreate::GDriveFileResumableCreate(const QString &filepath,
                                                             const QUrlQuery &args,
                                                             QOAuth2AuthorizationCodeFlow *parent)
    :GDriveFileTask (parent),m_file(filepath,this),m_status(Initial)
{
    m_initialUrl = buildUrl("resumable",args);
}

GDrive::GDriveFileResumableCreate::~GDriveFileResumableCreate()
{

}

QByteArray GDrive::GDriveFileResumableCreate::getReplyString() const
{
    return m_replyData;
}

QUrlQuery GDrive::GDriveFileResumableCreate::buildUrlArgs(const bool enforceSingleParent,
                                                          const bool ignoreDefaultVisibility,
                                                          const bool keepRevisionForever,
                                                          const QString &ocrLanguage,
                                                          const bool supportsAllDrives,
                                                          const bool useContentAsIndexableText)
{
    QUrlQuery args;
    args.addQueryItem("uploadType","resumable");
    if(enforceSingleParent){
        args.addQueryItem("enforceSingleParent",
                          QVariant(enforceSingleParent).toString());
    }
    if(ignoreDefaultVisibility){
        args.addQueryItem("ignoreDefaultVisibility",
                           QVariant().toString());
    }
    if(keepRevisionForever){
        args.addQueryItem("keepRevisionForever",
                           QVariant().toString());
    }
    if(!ocrLanguage.isEmpty()){
        args.addQueryItem("ocrLanguage",ocrLanguage);
    }
    if(supportsAllDrives){
        args.addQueryItem("supportsAllDrives",
                           QVariant(supportsAllDrives).toString());
    }
    if(useContentAsIndexableText){
        args.addQueryItem("useContentAsIndexableText",
                           QVariant(useContentAsIndexableText).toString());
    }
    return args;
}

bool GDrive::GDriveFileResumableCreate::start()
{
    if(m_status != UploadStatus::Initial){
        qWarning() << Q_FUNC_INFO << "Upload already";
        return false;
    }
    /* check open file */
    if(!m_file.isOpen() && !m_file.open(QIODevice::ReadOnly)){ // file is close and file open fail
        m_errStr += m_file.errorString();
        taskFailed();
        return false;
    }
    m_currentReply = requaetInitialSession(m_initialUrl);
    return true;
}

void GDrive::GDriveFileResumableCreate::abort()
{
    if(this->isComplete()){
        return;
    }
    /* disconnect and abort reply, so we don't need change those slot code */
    if(m_currentReply){
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
    /* close file */
    m_file.close();
    /* mark error string */
    m_errStr = QString("Abort upload.");
    /* emit finished signal */
    taskFailed();
}

void GDrive::GDriveFileResumableCreate::pause()
{
    /* The pause action will only enable when upload has started.
     * pause() is similar to abort(), but its not error */
    if((m_status == UploadStatus::RequestStartUpload) |
            (m_status == UploadStatus::RequestUploadprogress) |
            (m_status == UploadStatus::RequestResumeUpload))
    {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_status = UploadStatus::Paused;
        qInfo() << "Upload has been paused";
    }
}

void GDrive::GDriveFileResumableCreate::resume()
{
    if(m_status == UploadStatus::Paused){
        nextStep(RequestUploadprogress);
    }
}

QUrl GDrive::GDriveFileResumableCreate::buildUrl(const QString &uploadType) const
{
    QUrlQuery query;
    //https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&access_token=%1
    query.addQueryItem("uploadType",uploadType);
    auto url = QUrl("https://www.googleapis.com/upload/drive/v3/files");
    url.setQuery(query);
    return url;
}

QUrl GDrive::GDriveFileResumableCreate::buildUrl(const QString &uploadType, QUrlQuery args) const
{
    QUrl url("https://www.googleapis.com/upload/drive/v3/files");
    args.addQueryItem("uploadType",uploadType);
    url.setQuery(args);
    return url;
}

QNetworkReply *GDrive::GDriveFileResumableCreate::requaetInitialSession(const QUrl &url)
{
    qInfo() << Q_FUNC_INFO;
    /* collect file info */
    const QString fileName = QFileInfo(m_file).baseName();
    const QString fileMimeType = QMimeDatabase().mimeTypeForFile(m_file).name();
    const qint64 fileSize = m_file.size();
    /* Build request */
    QNetworkRequest request(url);
    const QByteArray body = QString("{\"name\":\"%1\"}").arg(fileName).toUtf8();
    request.setHeader(QNetworkRequest::ContentLengthHeader,body.size());
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json; charset=UTF-8");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(mp_google->token()).toLatin1());
    request.setRawHeader("X-Upload-Content-Type",fileMimeType.toLatin1());
    request.setRawHeader("X-Upload-Content-Length",QByteArray::number(fileSize));
    /* Send request */
    auto reply = mp_google->networkAccessManager()->post(request,body);
    connect(reply,&QNetworkReply::finished,
            this,&GDriveFileResumableCreate::onInitialSession_ReplyFinished);
    connect(reply,QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this,&GDriveFileResumableCreate::onInitialSession_ReplyError);
    /* emit uploadProgress signal,so ui like progressDialog will be open */
    emit uploadProgress(0,m_file.size());
    return reply;
}

QNetworkReply *GDrive::GDriveFileResumableCreate::requestStartUpload(const QUrl &sessionUrl)
{
    qInfo() << Q_FUNC_INFO;
    /* collect file info */
    const QString fileMimeType = QMimeDatabase().mimeTypeForFile(m_file).name();
    /* build request */
    QNetworkRequest request(sessionUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,fileMimeType.toLatin1());
    request.setHeader(QNetworkRequest::ContentLengthHeader,m_file.size());
    /* connect reply and slot */
    auto reply = mp_google->networkAccessManager()->put(request,&m_file);
    connect(reply,&QNetworkReply::finished,
            this,&GDriveFileResumableCreate::onStartUpload_ReplyFinished);
    connect(reply,QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this,&GDriveFileResumableCreate::onStartUpload_ReplyError);
    connect(reply,&QNetworkReply::uploadProgress,
            this,&GDriveFileResumableCreate::uploadProgress);
    return reply;
}

QNetworkReply *GDrive::GDriveFileResumableCreate::requestUploadProgress(const QUrl &sessionUrl)
{
    qInfo() << Q_FUNC_INFO;
    /* collect file info */
    const QByteArray rangeVal = "bytes */" + QByteArray::number(m_file.size());
    /* construct request */
    QNetworkRequest request(sessionUrl);
    request.setHeader(QNetworkRequest::ContentLengthHeader,0);
    request.setRawHeader("Content-Range",rangeVal);
    /* send request without request body */
    auto reply = mp_google->networkAccessManager()->put(request,QByteArray());
    connect(reply,&QNetworkReply::finished,
            this,&GDriveFileResumableCreate::onStartUpload_ReplyFinished);
    connect(reply,QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this,&GDriveFileResumableCreate::onUploadProgress_ReplyError);
    return reply;
}

QNetworkReply *GDrive::GDriveFileResumableCreate::requestResumeUpload(const QUrl &sessionUrl, const qint64 offset)
{
    qInfo() << Q_FUNC_INFO;
    //! collect file info
    const qint64 contentlenVal = m_file.size() - (offset+1);
    const QByteArray contentrangeVal =
            QString("bytes %1-%2/%3").arg(offset+1 , m_file.size()-1 , m_file.size()).toLatin1();
    QNetworkRequest request(sessionUrl);
    request.setHeader(QNetworkRequest::ContentLengthHeader,contentlenVal);
    request.setRawHeader("Content-Range",contentrangeVal);
    //! send request
    auto reply = mp_google->networkAccessManager()->put(request,&m_file);
    connect(reply,&QNetworkReply::finished,
            this,&GDriveFileResumableCreate::onResumeUpload_ReplyFinished);
    connect(reply,QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this,&GDriveFileResumableCreate::onResumeUpload_ReplyError);
    connect(reply,&QNetworkReply::uploadProgress,
            this,&GDriveFileResumableCreate::onResumeUpload_uploadProgress);
    return reply;
}

qint64 GDrive::GDriveFileResumableCreate::getContentRangeMax(const QNetworkReply *reply) const
{
    QRegularExpression pattern("Range: bytes=0-(?<offset>\\d+)");
    auto match = pattern.match(reply->rawHeader("Content-Range")); //"Range: bytes=0-XX"
    if(match.hasMatch()){
        const qint64 offset = match.captured("offset").toLongLong();
        return offset;
    }else {
        qWarning() << "Match failed:" << reply->rawHeader("Content-Range");
        return -1;
    }
}

void GDrive::GDriveFileResumableCreate::restart()
{
    if(m_status != UploadStatus::Initial){
        m_sessionUri.clear();
        m_status = UploadStatus::Initial;
        m_offset = 0;
    }
    start();
}

void GDrive::GDriveFileResumableCreate::nextStep(GDrive::GDriveFileResumableCreate::UploadStatus status)
{
    m_status = status;
    if(status == RequestStartUpload){ // after Initial session uri received
        m_currentReply = requestStartUpload(m_sessionUri);
    }else if(status == RequestUploadprogress){
        m_currentReply = requestUploadProgress(m_sessionUri);
    }else if (status == RequestResumeUpload) {
        m_currentReply = requestResumeUpload(m_sessionUri,m_offset);
    }
}

void GDrive::GDriveFileResumableCreate::taskFailed()
{
    m_status = UploadStatus::Error;
    GDriveFileTask::taskFailed();
}

void GDrive::GDriveFileResumableCreate::taskSucceeded()
{
    m_status = UploadStatus::Success;
    GDriveFileTask::taskSucceeded();
}

void GDrive::GDriveFileResumableCreate::onInitialSession_ReplyFinished()
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply->error()!=QNetworkReply::NoError){
        return;
    }
    /* save sessionUri URI */
    m_sessionUri = reply->header(QNetworkRequest::LocationHeader).toUrl();
    /* continue to request upload start */
    nextStep(RequestStartUpload);
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onInitialSession_ReplyError(QNetworkReply::NetworkError code)
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qWarning() << "[Error]Reply error code:" << httpStatus << " " << reply->errorString();
    if(httpStatus == (500|502|503|504|403)){   // retry request
        restart();
    }else if(httpStatus == 404){               // restart upload
        restart();
    }else {                                    // Bad End
        taskFailed();
    }
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onStartUpload_ReplyFinished()
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply->error()!=QNetworkReply::NoError){
        qWarning() << "[Error]Resumable Upload reply error: " << reply->errorString();
        return;
    }
    /* upload success: save file resource, set state, emit finished */
    m_replyData = reply->readAll();
    taskSucceeded();
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onStartUpload_ReplyError(QNetworkReply::NetworkError code)
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qWarning() << "[Error]Reply error code:" << httpStatus << " " << reply->errorString();
    if(httpStatus == 503){                          // request resume upload
        nextStep(RequestUploadprogress);
    }else if(httpStatus == (500|502|504|403)){      // request retry/resume upload
        nextStep(RequestUploadprogress);            // request resume upload, save upload time(unless session Uri expried)
    }else if(httpStatus == 404){                    // restart upload
        restart();
    }else {                                         // Bad End
        taskFailed();
    }
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onUploadProgress_ReplyFinished()
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply->error()!=QNetworkReply::NoError){
        return;
    }
    /* 200 means upload has complete */
    m_replyData = reply->readAll();
    taskSucceeded();
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onUploadProgress_ReplyError(QNetworkReply::NetworkError code)
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qWarning() << "[Error]Reply error code:" << httpStatus << " " << reply->errorString();
    if(httpStatus == 308){                             //continue upload
        qint64 offset = getContentRangeMax(reply);
        if(offset == -1){
            taskFailed();
        }else{
            m_offset = offset;
            nextStep(RequestResumeUpload);
        }
    }else if (httpStatus == (500|502|503|504|403)) {   //Resume
        nextStep(RequestUploadprogress);
    }else if (httpStatus == 404) {                     //Restart
        restart();
    }else {                                            //Badend
        taskFailed();
    }
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onResumeUpload_ReplyFinished()
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply->error()!=QNetworkReply::NoError){
        return;
    }
    m_replyData = reply->readAll();
    taskSucceeded();
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onResumeUpload_ReplyError(QNetworkReply::NetworkError code)
{
    qInfo() << Q_FUNC_INFO;
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qWarning() << "[Error]Reply error code:" << httpStatus << " " << reply->errorString();
    if(httpStatus == (500|502|503|504|403)){   // retry/resume request
        nextStep(RequestUploadprogress);
    }else if(httpStatus == 404){               // restart upload
        restart();
    }else {                                    // Bad End
        taskFailed();
    }
    reply->deleteLater();
}

void GDrive::GDriveFileResumableCreate::onResumeUpload_uploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    /* add the sent byte */
    emit uploadProgress(bytesSent + m_offset, bytesTotal + m_offset);
}
