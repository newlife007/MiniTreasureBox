#pragma once

class QWidget;

namespace mtb::tools {

[[nodiscard]] QWidget* createJsonWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createBase64Widget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createUrlWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createTimestampWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createUuidWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createTextStatisticsWidget(QWidget* parent = nullptr);

}  // namespace mtb::tools
