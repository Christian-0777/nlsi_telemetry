#include "IconTheme.h"

#include <QPainter>
#include <QFile>
#include <QIODevice>
#include <QPixmap>
#include <QSvgRenderer>

namespace nlsi::gui {
namespace {

constexpr auto kDefaultIconColor = "#F896B9";
constexpr auto kSelectedIconColor = "#FFFFFF";

QIcon RenderIcon(const QString& name, const QString& color) {
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = file.readAll();
    svg.replace(kDefaultIconColor, color.toLatin1());
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }

    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0.0, 0.0, 24.0, 24.0));
    painter.end();
    return QIcon(pixmap);
}

} // namespace

QIcon NavigationIcon(const QString& name, bool selected) {
    return RenderIcon(
        name,
        selected ? QString::fromLatin1(kSelectedIconColor)
                 : QString::fromLatin1(kDefaultIconColor));
}

} // namespace nlsi::gui
