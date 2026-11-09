#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QDateTime>
#include <QProgressBar>
#include <QFileInfo>
#include <QUrl>
#include <QInputDialog>
#include <QList>
#include <QDir>
#include <QCloseEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    struct chunk {
        qint64 start = 0;
        qint64 end = 0;
        qint64 receive = 0;
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
    };

    enum down_mode
    {
        kongxian,
        danxiancheng,
        duoxiancheng
    };

private slots:
    void reply_finish(QNetworkReply* reply);
    void on_check_file_clicked();
    void on_cls_log_clicked();
    void on_down_one_clicked();
    void on_file_down_clicked();
    void on_down_many_clicked();
    void start_download(const QString &url , qint64 file_big , const QString &savepath);
    bool normalize_path(QString &path, const QString &url, QString &err);
    void start_many_download(const QString &url , qint64 file_big , const QString &savepath , int xiancheng);

    void on_pause_clicked();
    void pause_danxiancheng();
    void pause_duoxiancheng();
    void huifu_danxiancheng();
    void huifu_duoxiancheng();
    void stop_danxiancheng();
    void stop_duoxiancheng();
    void on_stop_clicked();
    void huifu_reply(QNetworkReply *reply);
    void huifu_chunk(int index);
    void closeEvent(QCloseEvent *event);

private:
    Ui::MainWindow *ui;
    QNetworkAccessManager *cmd = nullptr;

    qint64 file_all = -1;
    bool support = false;
    QString check_url;

    QFile *m_file = nullptr;
    QNetworkReply *m_reply = nullptr;
    qint64 m_last_bytes = 0;
    qint64 m_last_time = 0;

    QList<chunk> m_chunks;
    QString m_many_path;
    qint64 m_many_file_big = 0;
    qint64 m_many_file_done = 0;
    qint64 m_many_last_time = 0;
    qint64 m_many_last_bytes = 0;
    bool m_error = false;

    down_mode m_mode = kongxian;
    bool m_pause = false;
    bool m_stop = false;
    QString m_url_;
    QString m_path_;

    qint64 m_one_ok_bytes = 0;

    // 正在退出：为 true 时 finished 回调不再做清理，交给 closeEvent 统一处理
    bool m_quitting = false;
};
#endif // MAINWINDOW_H
