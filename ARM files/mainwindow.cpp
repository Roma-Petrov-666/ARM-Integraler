#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Lexer.h"
#include "Parser.h"
#include "ExprTree.h"
#include "IntegralClassifier.h"
#include <QMessageBox>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    showMaximized();
    connect(ui->lineEdit, &QLineEdit::returnPressed, this, &MainWindow::on_pushButton_clicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_clearButton_clicked()
{
    ui->lineEdit->clear();
    ui->label->setText("Результат появится здесь...");
}

void MainWindow::on_pushButton_clicked()
{
    QString input = ui->lineEdit->text().trimmed();
    if (input.isEmpty()) {
        ui->label->setText("Введите выражение.");
        return;
    }

    std::string expr = input.toStdString();

    try {
        Lexer lexer(expr);
        std::vector<Token> tokens = lexer.tokenize();

        for (const auto& t : tokens) {
            if (t.type == TokenType::ERROR) {
                throw std::runtime_error(
                    "Ошибка в токене '" + t.value + "' на позиции " + std::to_string(t.position));
            }
        }

        Parser parser(tokens);
        ExprTree tree(parser.parse());
        tree.normalize();

        auto solutions = integrateWithSteps(tree.root(), "x");

        if (solutions.empty()) {
            ui->label->setText("Интеграл не выражается в элементарных функциях (не решаем).");
        } else {
            QString html;
            if (solutions.size() == 1) {
                html += "<h2>✅ Решение</h2>";
                const auto& steps = solutions[0];
                for (size_t i = 0; i < steps.size() - 1; ++i) {
                    html += "▸ " + QString::fromStdString(steps[i].description) + "<br>";
                }
                html += "<hr>";
                if (!steps.empty() && steps.back().result) {
                    html += "<p style='font-size:18pt; font-weight:bold;'>Результат:<br>";
                    html += QString::fromStdString(steps.back().result->print()) + " + C</p>";
                }
            } else {
                html += "<h2>🔍 Найдено несколько решений</h2>";
                for (size_t k = 0; k < solutions.size(); ++k) {
                    html += "<h3>📌 Вариант " + QString::number(k+1) + "</h3>";
                    const auto& steps = solutions[k];
                    for (size_t i = 0; i < steps.size() - 1; ++i) {
                        html += "▸ " + QString::fromStdString(steps[i].description) + "<br>";
                    }
                    if (!steps.empty() && steps.back().result) {
                        html += "<p style='font-size:14pt; font-weight:bold;'>Результат: ";
                        html += QString::fromStdString(steps.back().result->print()) + " + C</p>";
                    }
                    html += "<br>";
                }
            }
            ui->label->setText(html);
        }
    }
    catch (const std::exception& e) {
        QMessageBox::warning(this, "Ошибка", QString::fromStdString(e.what()));
        ui->label->clear();
    }
    catch (...) {
        QMessageBox::critical(this, "Ошибка", "Неизвестная ошибка.");
        ui->label->clear();
    }
}