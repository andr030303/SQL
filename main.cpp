#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <sqlite3.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Browser.H>
#include <FL/fl_ask.H>

// ==========================================
// 4. ДЕМОНСТРАЦИЯ ИСКЛЮЧЕНИЙ
// ==========================================
class DatabaseException : public std::runtime_error {
public:
    explicit DatabaseException(const std::string& message) 
        : std::runtime_error("БД Ошибка: " + message) {}
};

// ==========================================
// 2. СВОЙ РОДИТЕЛЬСКИЙ КЛАСС
// ==========================================
class DbEntity {
protected:
    int id;
public:
    explicit DbEntity(int id) : id(id) {}
    virtual ~DbEntity() = default;

    // Полиморфный метод
    virtual std::string getDisplayString() const = 0;
    int getId() const { return id; }
};

// Дочерний класс (Наследование от своего класса)
class PersonEntity : public DbEntity {
private:
    std::string name;
    int age;

public:
    PersonEntity(int id, std::string name, int age) 
        : DbEntity(id), name(std::move(name)), age(age) {}

    std::string getDisplayString() const override {
        return "ID: " + std::to_string(id) + " | Имя: " + name + " | Возраст: " + std::to_string(age);
    }

    const std::string& getName() const { return name; }
    int getAge() const { return age; }

    // ==========================================
    // 3. ПЕРЕГРУЗКА 3-х ОПЕРАТОРОВ
    // ==========================================
    
    // 1. Оператор сравнения (сравниваем по ID)
    bool operator==(const PersonEntity& other) const {
        return this->id == other.id;
    }

    // 2. Оператор присваивания
    PersonEntity& operator=(const PersonEntity& other) {
        if (this != &other) {
            this->id = other.id;
            this->name = other.name;
            this->age = other.age;
        }
        return *this;
    }

    // 3. Оператор вывода в поток (для логгирования)
    friend std::ostream& operator<<(std::ostream& os, const PersonEntity& p) {
        os << "Person[id=" << p.id << ", name=" << p.name << ", age=" << p.age << "]";
        return os;
    }
};

// ==========================================
// БИЗНЕС-ЛОГИКА: УПРАВЛЕНИЕ БАЗОЙ ДАННЫХ
// ==========================================
class DatabaseManager {
private:
    // Custom deleter для корректного освобождения ресурса SQLite умным указателем
    struct SqliteDeleter {
        void operator()(sqlite3* db) const { sqlite3_close(db); }
    };

    // 6. Использование только умных указателей (для владения БД)
    std::unique_ptr<sqlite3, SqliteDeleter> db;

    void execute(const std::string& sql) {
        char* err_msg = nullptr;
        // Здесь сырой указатель db.get() требуется сторонним C-API SQLite
        if (sqlite3_exec(db.get(), sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::string err = err_msg;
            sqlite3_free(err_msg);
            throw DatabaseException(err); // Выброс исключения
        }
    }

public:
    void openOrCreate(const std::string& filename) {
        sqlite3* raw_db = nullptr;
        if (sqlite3_open(filename.c_str(), &raw_db) != SQLITE_OK) {
            std::string err = raw_db ? sqlite3_errmsg(raw_db) : "Unknown error";
            sqlite3_close(raw_db);
            throw DatabaseException("Не удалось открыть файл: " + err);
        }
        db.reset(raw_db);

        // Инициализация таблицы
        execute("CREATE TABLE IF NOT EXISTS persons (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INTEGER);");
    }

    void insertRecord(const std::string& name, int age) {
        std::string sql = "INSERT INTO persons (name, age) VALUES ('" + name + "', " + std::to_string(age) + ");";
        execute(sql);
    }

    void updateRecord(int id, const std::string& name, int age) {
        std::string sql = "UPDATE persons SET name = '" + name + "', age = " + std::to_string(age) + " WHERE id = " + std::to_string(id) + ";";
        execute(sql);
    }

    // Возвращаем список умных указателей на БАЗОВЫЙ класс (UPCAST)
    std::vector<std::shared_ptr<DbEntity>> fetchAll() {
        std::vector<std::shared_ptr<DbEntity>> records;
        const char* sql = "SELECT id, name, age FROM persons;";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw DatabaseException("Ошибка подготовки SELECT запроса");
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const unsigned char* text = sqlite3_column_text(stmt, 1);
            std::string name = text ? reinterpret_cast<const char*>(text) : "";
            int age = sqlite3_column_int(stmt, 2);

            // 5. UPCAST: Создаем PersonEntity, но кладем в указатель на DbEntity
            std::shared_ptr<DbEntity> entity = std::make_shared<PersonEntity>(id, name, age);
            records.push_back(entity);
        }
        sqlite3_finalize(stmt);
        return records;
    }
};

// ==========================================
// 1. НАСЛЕДОВАНИЕ ОТ КЛАССОВ FLTK (Графическая обертка)
// ==========================================
class MainWindow : public Fl_Double_Window {
private:
    std::shared_ptr<DatabaseManager> dbManager;
    std::vector<std::shared_ptr<DbEntity>> currentData;

    // Сырые указатели допустимы здесь, т.к. FLTK берет на себя управление
    // их памятью (через деструктор родительского окна)
    Fl_Input* inputDbName;
    Fl_Button* btnOpen;
    Fl_Browser* browser;
    
    Fl_Input* inputName;
    Fl_Int_Input* inputAge;
    Fl_Button* btnAdd;
    Fl_Button* btnEdit;

    int selectedId = -1;

public:
    MainWindow() : Fl_Double_Window(500, 420, "SQLite C++ Wrapper"), dbManager(std::make_shared<DatabaseManager>()) {
        
        inputDbName = new Fl_Input(100, 10, 200, 25, "Файл БД:");
        inputDbName->value("test.db");
        
        btnOpen = new Fl_Button(310, 10, 180, 25, "Открыть / Создать");
        btnOpen->callback(cb_open, this);

        browser = new Fl_Browser(10, 50, 480, 200);
        browser->type(FL_HOLD_BROWSER);
        browser->callback(cb_browser_select, this);

        inputName = new Fl_Input(100, 270, 390, 25, "Имя:");
        inputAge = new Fl_Int_Input(100, 310, 100, 25, "Возраст:");

        btnAdd = new Fl_Button(100, 350, 180, 40, "Добавить запись");
        btnAdd->callback(cb_add, this);

        btnEdit = new Fl_Button(310, 350, 180, 40, "Сохранить правки");
        btnEdit->callback(cb_edit, this);
        btnEdit->deactivate(); // Выключен пока не выбрана запись

        end();
    }

private:
    void refreshBrowser() {
        browser->clear();
        currentData.clear();
        try {
            currentData = dbManager->fetchAll();
            for (const auto& record : currentData) {
                // Полиморфный вызов getDisplayString()
                browser->add(record->getDisplayString().c_str());
            }
        } catch (const DatabaseException& e) {
            fl_alert("%s", e.what());
        }
    }

    // Callbacks
    static void cb_open(Fl_Widget*, void* data) {
        auto* win = static_cast<MainWindow*>(data);
        try {
            win->dbManager->openOrCreate(win->inputDbName->value());
            fl_message("База данных успешно подключена!");
            win->refreshBrowser();
        } catch (const std::exception& e) {
            fl_alert("Ошибка: %s", e.what());
        }
    }

    static void cb_browser_select(Fl_Widget*, void* data) {
        auto* win = static_cast<MainWindow*>(data);
        int index = win->browser->value();
        if (index <= 0 || index > win->currentData.size()) return;

        // Получаем указатель на базовый класс
        std::shared_ptr<DbEntity> basePtr = win->currentData[index - 1];

        // 5. DOWNCAST: Преобразуем базовый указатель в указатель на потомка,
        // чтобы получить доступ к специфичным методам getName() и getAge()
        auto personPtr = std::dynamic_pointer_cast<PersonEntity>(basePtr);
        
        if (personPtr) {
            win->selectedId = personPtr->getId();
            win->inputName->value(personPtr->getName().c_str());
            win->inputAge->value(std::to_string(personPtr->getAge()).c_str());
            win->btnEdit->activate();
            
            // Демонстрация работы перегруженного оператора <<
            std::cout << "Выбрана запись: " << *personPtr << std::endl;
        }
    }

    static void cb_add(Fl_Widget*, void* data) {
        auto* win = static_cast<MainWindow*>(data);
        std::string name = win->inputName->value();
        std::string ageStr = win->inputAge->value();

        if (name.empty() || ageStr.empty()) {
            fl_alert("Заполните все поля!");
            return;
        }

        try {
            win->dbManager->insertRecord(name, std::stoi(ageStr));
            win->refreshBrowser();
            win->inputName->value("");
            win->inputAge->value("");
        } catch (const DatabaseException& e) {
            fl_alert("%s", e.what());
        }
    }

    static void cb_edit(Fl_Widget*, void* data) {
        auto* win = static_cast<MainWindow*>(data);
        if (win->selectedId == -1) return;

        std::string name = win->inputName->value();
        std::string ageStr = win->inputAge->value();

        try {
            win->dbManager->updateRecord(win->selectedId, name, std::stoi(ageStr));
            win->refreshBrowser();
            win->btnEdit->deactivate();
            win->selectedId = -1;
            win->inputName->value("");
            win->inputAge->value("");
        } catch (const std::exception& e) {
            fl_alert("%s", e.what());
        }
    }
};

int main(int argc, char **argv) {
    // Вся программа управляется через умный указатель
    auto mainWindow = std::make_unique<MainWindow>();
    mainWindow->show(argc, argv);
    return Fl::run();
}
