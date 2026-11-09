#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    cmd = new QNetworkAccessManager(this);
    ui->log->clear();
    QString check_url = "";
    ui->path->setText("C:/Users/windows/Downloads/");

    // ===================== 窗口图标 =====================
    // 用资源文件里的 png 设置窗口和任务栏图标
    QIcon appIcon(":/icons/app.png");
    setWindowIcon(appIcon);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_mode == kongxian)
    {
        event->accept();
        return;
    }

    auto ret = QMessageBox::question(this , "退出" , "当前有正在下载的任务在执行，退出将中断进程并删除残留文件，要退出吗" , QMessageBox::Yes | QMessageBox::No , QMessageBox::No);

    if (ret != QMessageBox::Yes)
    {
        event->ignore();
        return;
    }

    // ---- 从中止 -----
    // 先打上退出标记：让各 reply 的 finished 回调知道
    // "清理工作由 closeEvent 接管，你们不要动"
    m_quitting = true;

    if (m_mode == danxiancheng)
    {
        if (m_reply)
        {
            m_reply->abort();
        }
    }
    else
    {
        for (chunk &c : m_chunks)
        {
            if (c.reply)
            {
                c.reply->abort();
            }
        }
    }

    // abort() 之后各 reply 会陆续触发 finished 回调，
    // 那些回调里也会去关闭文件。这里如果立刻手动处理，
    // 会和回调抢同一批对象，导致崩溃/无响应。
    //
    // 所以先让事件循环把挂起的中止回调跑完，再统一清理。
    QCoreApplication::processEvents();

    // ---- 关闭所有文件句柄（必须判空，有的 chunk 可能已经清理过了）----
    if (m_file)
    {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }

    for (chunk &c : m_chunks)
    {
        if (c.file)
        {
            c.file->close();
            delete c.file;
            c.file = nullptr;
        }
    }

    // ---- 删除不完整的文件 ----
    QFile::remove(m_path_);
    QFile::remove(m_many_path);

    event->accept();
    return;
}

void MainWindow::start_many_download(const QString &url , qint64 file_big , const QString &savepath , int xiancheng)
{
    {
        if (!support)
        {
            ui->log->addItem("该服务器不支持分段下载，无法使用多线程");
            QMessageBox::warning(this, "不支持", "该服务器不支持分段下载");
            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);
            return;
        }

        if (file_big <= 0)
        {
            ui->log->addItem("文件大小未知，无法分段");
            QMessageBox::warning(this, "错误", "无法获取文件大小");
            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);
            return;
        }

        if (xiancheng <= 0)
        {
            ui->log->addItem("线程数无效");
            QMessageBox::critical(this , "错误" , "线程数无效");
            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);
            return;
        }

        ui->log->addItem("路径长度: " + QString::number(savepath.length()));
        ui->log->addItem("路径内容: [" + savepath + "]");
        ui->log->addItem("目录是否存在: " +
                         QString(QFileInfo(savepath).absolutePath()) +
                         " → " +
                         QString(QDir(QFileInfo(savepath).absolutePath()).exists() ? "是" : "否"));ui->log->addItem("尝试打开: [" + savepath + "]");

        QFile many(savepath);

        if (!many.open(QIODevice::WriteOnly))
        {
            ui->log->addItem("无法创建文件");
            ui->log->addItem("错误信息：" + many.errorString());
            QMessageBox::critical(this , "错误" , "无法创建文件");
            ui->log->addItem("  错误码: " + QString::number(many.error()));

            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);

            return;
        }

        many.resize(file_big);
        many.close();
    }

    m_many_path = savepath;
    m_many_file_big = file_big;
    m_many_file_done = 0;
    m_error = false;

    m_many_last_bytes = 0;
    m_many_last_time = QDateTime::currentMSecsSinceEpoch();

    m_chunks.clear();

    ui->download_jindu->setMaximum(1000);
    ui->download_jindu->setValue(0);
    ui->download_sudu->setText("--.-- MB/s");

    const qint64 chunk_size = file_big / xiancheng;

    for (int i = 0 ; i < xiancheng ; i++)
    {
        chunk c;

        c.start = qint64(i) * xiancheng;
        c.end = (i == xiancheng - 1) ? (file_big - 1) : (c.start + chunk_size - 1);

        if (c.end < c.start)
        {
            ui->log->addItem("非法空段，已跳过");
            continue;
        }

        c.file = new QFile(savepath);

        if (!c.file->open(QIODevice::ReadWrite))
        {
            ui->log->addItem(QString("线程 %1 打开文件失败，错误信息： %2").arg(i).arg(c.file->errorString()));
            QMessageBox::critical(this , "错误" ,"无法打开文件");
            delete c.file;
            c.file = nullptr;
            continue;
        }

        c.file->seek(c.start);

        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("Range",QString("bytes=%1-%2").arg(c.start).arg(c.end).toUtf8());
        // 30 秒内没有任何数据就自动断开，防止某个线程挂死拖垮整个下载
        req.setTransferTimeout(30000);
        c.reply = cmd->get(req);
        m_chunks.append(c);
    }

    if (m_chunks.isEmpty())
    {
        ui->log->addItem("没有可用的下载段落");
        QMessageBox::critical(this , "错误" , "未能创建任何下载段落");

        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);

        return;
    }

    for (int i = 0 ; i < m_chunks.size() ; i++)
    {
        connect(m_chunks[i].reply , &QNetworkReply::readyRead , this , [this , i]()
        {
            chunk &c = m_chunks[i];

            if (m_error)
            {
                return;
            }

            if (!c.file)
            {
                return;
            }

            int code = c.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (code != 206)
            {
                ui->log->addItem(QString("线程 %1 收到状态码 %2 不为206，服务可能未正确支持分段").arg(i).arg(code));

                if (!m_error)
                {
                    m_error = true;

                    for (chunk &x : m_chunks)
                    {
                        if (x.reply)
                        {
                            x.reply->abort();
                        }
                    }

                    QMessageBox::critical(this , "错误" , "下载失败，服务器未正确响应分段请求，请改用单线程下载");
                    ui->log->addItem("服务器未正确响应分段请求，下载失败");
                    ui->log->addItem("状态码：" + QString::number(code));
                }
                // 不在这里恢复按钮：等所有线程的 finished 都跑完，
                // 由 finished 里的统一清理流程恢复，避免中途被误判"还有下载在进行"
                return;
            }

            QByteArray data = c.reply->readAll();
            c.file->write(data);

            c.receive += data.size();

            m_many_file_done += data.size();

            if (m_many_file_big > 0)
            {
                ui->download_jindu->setValue(int(m_many_file_done * 1000 / m_many_file_big));
            }

            qint64 now = QDateTime::currentMSecsSinceEpoch();
            qint64 dt = now - m_many_last_time;

            if (dt > 500)
            {
                double bytes = (m_many_file_done - m_many_last_bytes) * 1000.0 / dt;
                double mb = bytes / (1024.0 * 1024.0);

                ui->download_sudu->setText(QString("%1 MB/s").arg(mb, 0, 'f', 2));

                m_many_last_bytes = m_many_file_done;
                m_many_last_time = now;
            }
        });

        connect(m_chunks[i].reply , &QNetworkReply::finished , this , [this , i]()
        {
            chunk &c = m_chunks[i];

            // 正在退出：closeEvent 会统一清理文件、删残片、恢复按钮。
            // 这里只把 reply 标记释放，别碰 m_chunks 里的其他东西，
            // 否则会和 closeEvent 抢同一批对象导致崩溃。
            if (m_quitting)
            {
                if (c.reply)
                {
                    c.reply->deleteLater();
                    c.reply = nullptr;
                }
                return;
            }

            // 关掉本段的文件句柄（暂停时也要关，恢复时会重新打开）
            if (c.file)
            {
                c.file->close();
                delete c.file;
                c.file = nullptr;
            }

            // 真正的错误才记录，主动 abort 不算错误
            if (c.reply->error() != QNetworkReply::NoError &&
                c.reply->error() != QNetworkReply::OperationCanceledError)
            {
                ui->log->addItem(QString("线程%1失败").arg(i));
                ui->log->addItem("失败原因：" + c.reply->errorString());
            }

            c.reply->deleteLater();
            c.reply = nullptr;

            // 检查是否所有线程都结束了
            bool all_done = true;
            for (const chunk &x : m_chunks)
            {
                if (x.reply != nullptr)
                {
                    all_done = false;
                    break;
                }
            }

            if (!all_done)
            {
                return;     // 还有线程在跑，继续等
            }

            // ============ 全部结束，判断是什么原因结束的 ============

            if (m_pause)
            {
                // ---- 暂停 ----
                // 保留 m_chunks 里的进度（receive 字段），
                // 等用户点"继续"时按剩余部分重新请求。
                // 不恢复按钮、不切模式。
                ui->download_sudu->setText("--.-- MB/s");
                return;
            }

            if (m_stop)
            {
                // ---- 停止 ----
                // 删掉不完整的文件，别留个半截货在硬盘上
                QFile::remove(m_many_path);

                ui->log->addItem("已停止，不完整文件已删除");

                m_stop = false;
                m_pause = false;
                m_mode = kongxian;

                m_chunks.clear();
                m_error = false;

                ui->download_sudu->setText("--.-- MB/s");
                ui->down_one->setEnabled(true);
                ui->down_many->setEnabled(true);
                return;
            }

            if (m_error)
            {
                // ---- 服务器异常导致的中止 ----
                ui->log->addItem("下载已终止");
                ui->download_sudu->setText("--.-- MB/s");
            }
            else
            {
                // ---- 正常完成 ----
                ui->log->addItem("下载完成");
                ui->download_jindu->setValue(ui->download_jindu->maximum());
                ui->download_sudu->setText("--.-- MB/s");

                // 核对最终文件大小，防止静默丢字节
                QFileInfo fi(m_many_path);
                if (fi.size() != m_many_file_big)
                {
                    ui->log->addItem(QString("文件大小 %1 与预期大小 %2 不符").arg(fi.size()).arg(m_many_file_big));
                    QMessageBox::warning(this , "警告" , "文件大小与预期大小不符，详见日志");
                }
            }

            m_chunks.clear();
            m_error = false;
            m_pause = false;
            m_stop = false;
            m_mode = kongxian;

            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);
        });
    }

    ui->log->addItem(QString("已启动 %1 个线程").arg(m_chunks.size()));
}

void MainWindow::on_check_file_clicked()
{
    ui->log->addItem("开始");
    QString url = ui->url->text().trimmed();

    if (url.isEmpty())
    {
        QMessageBox::critical(this,"提示","未输入URL地址");
        ui->log->addItem("URL为空");
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Range","bytes=0-0");

    QNetworkReply *reply = cmd->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply_finish(reply);
    });
}

void MainWindow::reply_finish(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        ui->log->addItem("请求失败");
        ui->log->addItem("错误信息" + reply->errorString());
        reply->deleteLater();
        QMessageBox::critical(this,"请求结束","请求失败");
        return;
    }

    QString cr = reply->rawHeader("Content-Range");

    if (!cr.isEmpty())
    {
        int a = cr.indexOf('/');
        if (a > 0)
        {
            file_all = cr.mid(a + 1).toLongLong();
            support = true;
        }
    }

    if (file_all < 0)
    {
        file_all = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    }

    check_url = ui->url->text().trimmed();

    ui->log->addItem("文件大小：" + QString::number(file_all) + "字节");
    ui->log->addItem("支持分段下载：" + QString(support ? "是" : "否"));

    reply->deleteLater();
    return;
}

void MainWindow::on_cls_log_clicked()
{
    ui->log->clear();
    return;
}


// 把用户输入的路径规范成"完整文件路径"
//   输入 E:\                     → E:\<URL里的文件名>
//   输入 E:\新建文件夹\           → E:\新建文件夹\<URL里的文件名>
//   输入 E:\bili.exe              → 原样返回
//   输入 E:\bili（没后缀）         → 补上 URL 里的后缀，仍然没后缀就报错
bool MainWindow::normalize_path(QString &path, const QString &url, QString &err)
{
    path = path.trimmed();

    // 情况一：完全没填
    if (path.isEmpty())
    {
        err = "下载位置为空";
        return false;
    }

    // 统一分隔符，方便后面判断
    QString normalized = path;
    normalized.replace('\\', '/');

    // 从 URL 里抠出文件名，比如 .../bili_win-install.exe → bili_win-install.exe
    QString urlName = QFileInfo(QUrl(url).path()).fileName();
    if (urlName.isEmpty())
    {
        urlName = "download.bin";   // URL 里没名字，给个兜底
    }

    QFileInfo fi(normalized);

    // 情况二：本身就是目录（已存在），或者结尾带斜杠（可能还没创建）
    if (fi.isDir() || normalized.endsWith('/'))
    {
        // 去掉末尾多余斜杠，避免出现 E:\\\name
        while (normalized.endsWith('/') && normalized.size() > 1)
        {
            normalized.chop(1);
        }
        path = normalized + "/" + urlName;
        return true;
    }

    // 情况三：有目录部分，但文件名没写后缀
    //   fi.suffix() 为空 → 说明最后一段没有 .xxx
    if (fi.suffix().isEmpty())
    {
        // 用 URL 里的后缀补上
        QString urlSuffix = QFileInfo(urlName).suffix();
        if (urlSuffix.isEmpty())
        {
            err = "文件名缺少后缀（如 .exe / .zip），请补全或直接选择目录";
            return false;
        }
        path = normalized + "." + urlSuffix;
        return true;
    }

    // 情况四：已经是完整文件名，原样使用
    path = normalized;
    return true;
}

void MainWindow::on_down_one_clicked()
{
    if (file_all < 0)
    {
        ui->log->addItem("步骤缺失");
        QMessageBox::warning(this,"步骤缺失","请先点击“检测文件信息”按钮");
        return;
    }

    if (check_url != ui->url->text().trimmed())
    {
        ui->log->addItem("URL地址变更，请重试");
        QMessageBox::warning(this,"步骤缺失","URL地址变更");
        return;
    }

    // ---- 路径规范化 ----
    QString save_path = ui->path->text();
    QString pathErr;

    if (!normalize_path(save_path, ui->url->text().trimmed(), pathErr))
    {
        ui->log->addItem("路径错误：" + pathErr);
        QMessageBox::critical(this, "错误", pathErr);
        return;
    }

    // 把补全后的路径写回输入框，让用户看得见实际会存到哪
    ui->path->setText(save_path);

    ui->down_one->setEnabled(false);
    ui->down_many->setEnabled(false);

    // 记录本次下载的模式和关键信息，暂停/继续/停止都要用到
    m_mode   = danxiancheng;
    m_url_   = ui->url->text().trimmed();
    m_path_  = save_path;
    m_pause  = false;
    m_stop   = false;
    m_one_ok_bytes = 0;

    ui->log->addItem("开始下载：" + save_path);
    start_download(ui->url->text().trimmed(), file_all, save_path);
}

void MainWindow::start_download(const QString &url , qint64 file_big , const QString &savepath)
{
    if (m_reply != nullptr)
    {
        ui->log->addItem("当前有线程正在下载");
        QMessageBox::critical(this , "错误" , "当前有线程正在下载");

        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);

        return;
    }
    m_file = new QFile(savepath);

    if (!m_file->open(QIODevice::WriteOnly))
    {
        ui->log->addItem("无法创建文件");
        QMessageBox::critical(this , "错误" , "无法创建文件");
        delete m_file;
        m_file = nullptr;
        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);
        return;
    }

    if (file_big > 0)
    {
        m_file->resize(file_big);
        ui->log->addItem("预分配完成");
    }
    else
    {
        ui->log->addItem("服务器不支持分段/无法拿到文件准确大小，已跳过预分配");
    }

    m_last_bytes = 0;
    m_last_time = QDateTime::currentMSecsSinceEpoch();

    QNetworkRequest req{QUrl(url)};
    // 30 秒内没有任何数据就自动断开，防止网络卡住时一直挂着
    req.setTransferTimeout(30000);
    m_reply = cmd->get(req);
    ui->log->addItem("发出HTTP请求");

    connect(m_reply , &QNetworkReply::readyRead , this , [this]() {
        if (m_file)
        {
            m_file->write(m_reply->readAll());
        }
    });

    connect(m_reply , &QNetworkReply::downloadProgress , this , [this](qint64 got , qint64 file_all)
    {
        if (file_all > 0)
        {
            ui->download_jindu->setMaximum(1000);
            ui->download_jindu->setValue(static_cast<int>(got*1000 / file_all));
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 dt = now - m_last_time;

        if (dt > 500)
        {
            qint64 ok_bytes = got - m_last_bytes;

            double bytes_V = ok_bytes * 1000.0 / dt;

            double MB_V = bytes_V / (1024.0 * 1024.0);

            ui->download_sudu->setText(QString("%1 MB/s").arg(MB_V , 0 , 'f' , 2));

            m_last_bytes = got;
            m_last_time = now;
        }
    });


    connect(m_reply , &QNetworkReply::finished , this , [this]()
    {
        if (m_file)
        {
            m_file->close();
            delete m_file;
            m_file = nullptr;
            ui->log->addItem("释放文件");
        }


        if (m_reply->error() != QNetworkReply::NoError)
        {
            ui->log->addItem("下载失败");
            ui->log->addItem("错误码：" + m_reply->errorString());
            QMessageBox::critical(this , "错误" , "下载失败");

            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);
        }
        else
        {
            ui->log->addItem("下载完成");
            ui->download_jindu->setValue(ui->download_jindu->maximum());
        }

        ui->download_sudu->setText("--.-- MB/s");

        m_reply->deleteLater();
        m_reply = nullptr;

        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);
    });

}
void MainWindow::on_file_down_clicked()
{
    QString file_ = QFileDialog::getSaveFileName(this , "选择文件保存位置" , "C:/Users/windows/Downloads/" ,  "所有文件（*.*）");
    if (!file_.isEmpty())
    {
        ui->path->setText(file_);
    }
    return;
}


void MainWindow::on_down_many_clicked()
{
    if (file_all < 0)
    {
        QMessageBox::warning(this, "步骤缺失", "请先点击“检测文件信息”按钮");
        return;
    }

    if (check_url != ui->url->text().trimmed())
    {
        QMessageBox::warning(this, "步骤缺失", "URL地址变更");
        return;
    }

    if (!support)
    {
        QMessageBox::warning(this, "不支持", "该服务器不支持分段下载");
        return;
    }

    if (m_reply != nullptr || !m_chunks.isEmpty())
    {
        QMessageBox::warning(this, "提示", "已有下载正在进行");
        return;
    }

    if (file_all <= 0)
    {
        ui->log->addItem("文件大小未知");
        QMessageBox::critical(this , "错误" , "无法获得文件大小");
        return;
    }

    if (m_reply != nullptr || !m_chunks.isEmpty())
    {
        QMessageBox::warning(this, "提示", "已有下载正在进行");
        return;
    }

    QString save_path = ui->path->text();
    QString pathErr;
    if (!normalize_path(save_path, ui->url->text().trimmed(), pathErr))
    {
        ui->log->addItem("路径错误：" + pathErr);
        QMessageBox::critical(this, "错误", pathErr);
        return;
    }
    ui->path->setText(save_path);

    bool ok = false;
    int n = QInputDialog::getInt(this , "选择线程数" , "下载线程数（1-128）:" , 16 , 1 , 128 , 1 , &ok);

    if (!ok || n <= 0)
    {
        ui->log->addItem("用户已取消");
        QMessageBox::warning(this , "提示" , "用户取消");
        return;
    }

    if (qint64(n) > file_all)
    {
        n = int(file_all > 0 ? file_all : 1);
    }

    if (!support)
    {
        QMessageBox::warning(this, "不支持", "该服务器不支持分段下载，请使用单线程");
        return;
    }

    ui->down_one->setEnabled(false);
    ui->down_many->setEnabled(false);

    // 记录本次下载的模式和关键信息，暂停/继续/停止都要用到
    m_mode  = duoxiancheng;
    m_url_  = ui->url->text().trimmed();
    m_path_ = save_path;
    m_pause = false;
    m_stop  = false;
    m_error = false;

    ui->log->addItem(QString("开始多线程下载，线程数：%1").arg(n));
    start_many_download(ui->url->text().trimmed(), file_all, save_path, n);
}


void MainWindow::on_pause_clicked()
{
    if (m_mode == kongxian)
    {
        ui->log->addItem("当前没有正在下载的任务");
        return;
    }

    if (!m_pause)
    {
        m_pause = true;
        ui->log->addItem("已暂停");

        if (m_mode == danxiancheng)
        {
            pause_danxiancheng();
        }
        else
        {
            pause_duoxiancheng();
        }
    }
    else
    {
        m_pause = false;
        ui->log->addItem("继续下载");

        if (m_mode == danxiancheng)
        {
            huifu_danxiancheng();
        }
        else
        {
            huifu_duoxiancheng();
        }
    }
}


void MainWindow::on_stop_clicked()
{
    if (m_mode == kongxian)
    {
        ui->log->addItem("当前没有正在下载的任务");
        return;
    }

    m_pause = false;
    m_stop = true;

    if (m_mode == danxiancheng)
    {
        stop_danxiancheng();
    }
    else
    {
        stop_duoxiancheng();
    }
}

void MainWindow::pause_danxiancheng()
{
    m_one_ok_bytes = m_file ? m_file->pos() : 0;

    if (m_reply)
    {
        m_reply->abort();
    }

    ui->log->addItem("已暂停，已下载" + QString::number(m_one_ok_bytes) + "字节");
    return;
}

void MainWindow::huifu_danxiancheng()
{
    m_file = new QFile(m_path_);

    if (!m_file->open(QIODevice::ReadWrite))
    {
        ui->log->addItem("无法打开文件");
        QMessageBox::critical(this , "错误" , "无法打开文件");

        m_pause = false;
        m_mode = kongxian;
        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);
        return;
    }

    m_file->seek(m_one_ok_bytes);

    QNetworkRequest req{QUrl(m_url_)};
    req.setRawHeader("Range" , QString("bytes=%1-").arg(m_one_ok_bytes).toUtf8());
    req.setTransferTimeout(30000);

    m_reply = cmd->get(req);
    huifu_reply(m_reply);

    ui->log->addItem("继续下载，从" + QString::number(m_one_ok_bytes) + "字节处开始");
    return;
}

void MainWindow::huifu_duoxiancheng()
{
    for (int i = 0 ; i < m_chunks.size() ; i++)
    {
        chunk &c = m_chunks[i];

        if (c.receive >= (c.end - c.start + 1))
        {
            continue;
        }

        qint64 new_start = c.start + c.receive;
        c.file = new QFile(m_path_);

        if (!c.file->open(QIODevice::ReadWrite))
        {
            ui->log->addItem(QString("线程 %1 恢复失败，无法打开文件").arg(i));
            QMessageBox::critical(this , "错误" , "线程恢复失败，详见日志");
            delete c.file;
            c.file = nullptr;
            continue;
        }

        c.file->seek(new_start);

        QNetworkRequest req{QUrl(m_url_)};
        req.setRawHeader("Range" , QString("bytes=%1-%2").arg(new_start).arg(c.end).toUtf8());
        req.setTransferTimeout(30000);

        c.reply = cmd->get(req);
        huifu_chunk(i);
    }
    ui->log->addItem("多线程下载恢复");
}

void MainWindow::stop_duoxiancheng()
{
    // 标记是否真的中断了请求。
    // 若处于暂停状态，各 chunk 的 reply 已经在暂停时被置空，
    // 这时 abort() 无事可做，也就不会触发 finished，
    // 必须手动走下面的清理流程。
    bool b = false;

    for (chunk &c : m_chunks)
    {
        if (c.reply)
        {
            c.reply->abort();
            b = true;       // 有活跃请求被中断，清理交给 finished
        }
    }

    if (b)
    {
        return;
    }

    for (chunk &c : m_chunks)
    {
        if (c.file)
        {
            c.file->close();
            delete c.file;
            c.file = nullptr;
        }
    }

    QFile::remove(m_many_path);

    m_chunks.clear();
    m_error = false;
    m_pause = false;
    m_stop = false;
    m_mode = kongxian;

    ui->log->addItem("已停止，不完整文件已删除");
    ui->download_sudu->setText("--.-- MB/s");

    ui->down_one->setEnabled(true);
    ui->down_many->setEnabled(true);
}

void MainWindow::huifu_reply(QNetworkReply *reply)
{
    connect(reply , &QNetworkReply::readyRead , this , [this , reply]()
    {
        if (m_file && reply == m_reply)
        {
            m_file->write(reply->readAll());
        }
    });

    connect(reply , &QNetworkReply::downloadProgress , this , [this , reply](qint64 got, qint64 file_all)
    {
        if (reply != m_reply)
        {
            return;
        }

        if (file_all > 0)
        {
            ui->download_jindu->setMaximum(1000);
            ui->download_jindu->setValue(static_cast<int>(got*1000 / file_all));
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 dt = now - m_last_time;

        if (dt > 500)
        {
            qint64 ok_bytes = got - m_last_bytes;

            double bytes_V = ok_bytes * 1000.0 / dt;

            double MB_V = bytes_V / (1024.0 * 1024.0);

            ui->download_sudu->setText(QString("%1 MB/s").arg(MB_V , 0 , 'f' , 2));

            m_last_bytes = got;
            m_last_time = now;
        }
    });

    connect(reply , &QNetworkReply::finished , this , [this , reply]()
    {
        if (reply != m_reply)
        {
            return;
        }

        // 正在退出：清理交给 closeEvent，这里只释放 reply
        if (m_quitting)
        {
            reply->deleteLater();
            m_reply = nullptr;
            return;
        }

        if (m_file)
        {
            m_file->close();
            delete m_file;
            m_file = nullptr;
        }

        const bool a = (reply->error() == QNetworkReply::OperationCanceledError);

        // ---- 先判断是不是用户主动中止的 ----
        // abort() 固定产生 OperationCanceledError，
        // 必须在"报错"判断之前把它识别出来，
        // 否则暂停/停止时日志会误报一条"下载失败"。
        if (a && m_pause)
        {
            reply->deleteLater();
            m_reply = nullptr;
            ui->download_sudu->setText("--.-- MB/s");
            return;
        }

        if (a && m_stop)
        {
            QFile::remove(m_path_);

            m_stop = false;
            m_pause = false;
            m_mode = kongxian;

            ui->log->addItem("已停止，残留文件已删除");
        }
        else if (reply->error() != QNetworkReply::NoError)
        {
            ui->log->addItem("下载失败，错误信息：" + reply->errorString());
            QMessageBox::critical(this , "错误" , "下载失败，详见日志");

            m_stop = false;
            m_pause = false;
            m_mode = kongxian;
        }
        else
        {
            ui->log->addItem("下载完成");
            ui->download_jindu->setValue(ui->download_jindu->maximum());

            m_stop = false;
            m_pause = false;
            m_mode = kongxian;
        }

        ui->download_sudu->setText("--.-- MB/s");

        reply->deleteLater();
        m_reply = nullptr;

        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);
    });
}

void MainWindow::huifu_chunk(int index)
{
    chunk &c = m_chunks[index];

    // ---- 数据到达 ----
    connect(c.reply , &QNetworkReply::readyRead , this , [this , index]()
    {
        chunk &cc = m_chunks[index];

        if (m_error || m_pause)
        {
            return;
        }

        if (!cc.file)
        {
            return;
        }

        // 状态码必须是 206，否则服务器没按分段返回
        int code = cc.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (code != 206)
        {
            ui->log->addItem(QString("线程 %1 收到状态码 %2 不为206").arg(index).arg(code));

            if (!m_error)
            {
                m_error = true;

                for (chunk &x : m_chunks)
                {
                    if (x.reply)
                    {
                        x.reply->abort();
                    }
                }

                QMessageBox::critical(this , "错误" , "下载失败，服务器未正确响应分段请求");
            }
            return;
        }

        QByteArray data = cc.reply->readAll();
        cc.file->write(data);

        // 累加本段已收字节数，这是下次恢复时的起点依据
        cc.receive += data.size();

        m_many_file_done += data.size();

        if (m_many_file_big > 0)
        {
            ui->download_jindu->setValue(int(m_many_file_done * 1000 / m_many_file_big));
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 dt = now - m_many_last_time;

        if (dt > 500)
        {
            double bytes = (m_many_file_done - m_many_last_bytes) * 1000.0 / dt;
            double mb = bytes / (1024.0 * 1024.0);

            ui->download_sudu->setText(QString("%1 MB/s").arg(mb, 0, 'f', 2));

            m_many_last_bytes = m_many_file_done;
            m_many_last_time = now;
        }
    });

    // ---- 本段结束 ----
    connect(c.reply , &QNetworkReply::finished , this , [this , index]()
    {
        chunk &cc = m_chunks[index];

        if (cc.file)
        {
            cc.file->close();
            delete cc.file;
            cc.file = nullptr;
        }

        if (cc.reply->error() != QNetworkReply::NoError &&
            cc.reply->error() != QNetworkReply::OperationCanceledError)
        {
            ui->log->addItem(QString("线程%1失败").arg(index));
            ui->log->addItem("失败原因：" + cc.reply->errorString());
        }

        cc.reply->deleteLater();
        cc.reply = nullptr;

        // 是否所有线程都结束了
        bool all_done = true;
        for (const chunk &x : m_chunks)
        {
            if (x.reply != nullptr)
            {
                all_done = false;
                break;
            }
        }

        if (!all_done)
        {
            return;
        }

        // 暂停：保留进度等继续
        if (m_pause)
        {
            ui->download_sudu->setText("--.-- MB/s");
            return;
        }

        // 停止：删文件
        if (m_stop)
        {
            QFile::remove(m_many_path);
            ui->log->addItem("已停止，不完整文件已删除");

            m_stop = false;
            m_pause = false;
            m_mode = kongxian;

            m_chunks.clear();
            m_error = false;

            ui->download_sudu->setText("--.-- MB/s");
            ui->down_one->setEnabled(true);
            ui->down_many->setEnabled(true);
            return;
        }

        // 异常中止
        if (m_error)
        {
            ui->log->addItem("下载已终止");
        }
        else
        {
            ui->log->addItem("下载完成");
            ui->download_jindu->setValue(ui->download_jindu->maximum());

            QFileInfo fi(m_many_path);
            if (fi.size() != m_many_file_big)
            {
                ui->log->addItem(QString("文件大小 %1 与预期大小 %2 不符").arg(fi.size()).arg(m_many_file_big));
            }
        }

        ui->download_sudu->setText("--.-- MB/s");

        m_chunks.clear();
        m_error = false;
        m_pause = false;
        m_stop = false;
        m_mode = kongxian;

        ui->down_one->setEnabled(true);
        ui->down_many->setEnabled(true);
    });
}

void MainWindow::stop_danxiancheng()
{
    if (m_reply)
    {
        m_reply->abort();
    }

    if (m_file)
    {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }

    QFile::remove(m_path_);

    m_stop = false;
    m_pause = false;
    m_mode = kongxian;

    ui->log->addItem("已停止，不完整文件已删除");
    ui->download_sudu->setText("--.-- MB/s");

    ui->down_one->setEnabled(true);
    ui->down_many->setEnabled(true);
}

void MainWindow::pause_duoxiancheng()
{
    for (chunk &c : m_chunks)
    {
        if (c.reply)
        {
            c.reply->abort();
        }
    }

    ui->log->addItem("已暂停");
    QMessageBox::information(this , "提示" , "已暂停");
    return;
}