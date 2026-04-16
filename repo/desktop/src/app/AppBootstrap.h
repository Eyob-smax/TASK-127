#pragma once

#include <memory>

class QSqlDatabase;
class AppSettings;
struct AppContext;

// Build and return AppContext. All infrastructure is initialized in dependency order.
// Returns nullptr on fatal initialization failure.
std::unique_ptr<AppContext> buildAppContext(QSqlDatabase& db,
                                            const AppSettings& settings);
