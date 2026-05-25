QT += widgets
CONFIG += c++17 console

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    Lexer.cpp \
    Parser.cpp \
    Normalizer.cpp \
    IntegralClassifier.cpp

HEADERS += \
    mainwindow.h \
    Lexer.h \
    Token.h \
    Ast.h \
    Parser.h \
    Normalizer.h \
    IntegralClassifier.h \
    ExprTree.h

FORMS += \
    mainwindow.ui