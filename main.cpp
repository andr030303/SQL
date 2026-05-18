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

class DbException : public std::runtime_error {
public:
    DbException(const std::string& message) : std::runtime_error("Ошибка Базы Данных: " + message) {}
};

class DbEntity {
protected:
    int id; // id записи в базе данных
public:
    DbEntity(int inputId) {
        this->id = inputId;
    }
    
    virtual ~DbEntity() {}

    virtual std::string getDisplayString() const = 0;
    
    int getId() const { 
        return id; 
    }
};

class PersonEntity : public DbEntity {
private:
    std::string name;
    int age;

public:
    PersonEntity(int inputId, const std::string& inputName, int inputAge) : DbEntity(inputId) 
    {
        this->name = inputName;
        this->age = inputAge;
    }

    std::string getDisplayString() const override {
        return "ID: " + std::to_string(id) + " | Имя: " + name + " | Возраст: " + std::to_string(age);
    }

    std::string getName() const { return name; }
    int getAge() const { return age; }

    // считаем, что люди равны, если равны их id
    bool operator==(const PersonEntity& other) const {
        return this->id == other.id;
    }

    PersonEntity& operator=(const PersonEntity& other) {
        if (this != &other) { // не присваивается ли объект самому себе
            this->id = other.id;
            this->name = other.name;
            this->age = other.age;
        }
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, const PersonEntity& p) {
        os << "Человек [ID=" << p.id << ", Имя=" << p.name << ", Возраст=" << p.age << "]";
        return os;
    }
};

// класс для работы с sqlite
class DatabaseManager {
private:
    struct SqliteDeleter {
        void operator()(sqlite3* db) const { 
            sqlite3_close(db); 
        }
    };

    std::unique_ptr<sqlite3, SqliteDeleter> db;

    void executeQuery(const std::string& sql) {
        // инициализирован ли указатель (открыта ли бд)
        if (db == nullptr) {
            throw DbException("База данных не подключена! Пожалуйста, сначала нажмите 'Подключиться'.");
        }

        char* errorMsg = nullptr;
        int result = sqlite3_exec(db.get(), sql.c_str(), nullptr, nullptr, &errorMsg);
        
        if (result != SQLITE_OK) {
            std::string errorString = errorMsg;
            sqlite3_free(errorMsg);
            throw DbException("Ошибка выполнения запроса: " + errorString);
        }
    }

public:
    void openOrCreate(const std::string& filename) {
        sqlite3* rawPointer = nullptr;
        int result = sqlite3_open(filename.c_str(), &rawPointer);
        
        if (result != SQLITE_OK) {
            std::string errorString = rawPointer ? sqlite3_errmsg(rawPointer) : "Неизвестная ошибка";
            sqlite3_close(rawPointer);
            throw DbException("Не удалось открыть БД: " + errorString);
        }
        
        db.reset(rawPointer);

        std::string createTableSql = "CREATE TABLE IF NOT EXISTS persons (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, age INTEGER);";
        executeQuery(createTableSql);
    }

    void insertRecord(const std::string& name, int age) {
        std::string sql = "INSERT INTO persons (name, age) VALUES ('" + name + "', " + std::to_string(age) + ");";
        executeQuery(sql);
    }

    void updateRecord(int id, const std::string& name, int age) {
        std::string sql = "UPDATE persons SET name = '" + name + "', age = " + std::to_string(age) + " WHERE id = " + std::to_string(id) + ";";
        executeQuery(sql);
    }

    std::vector<std::shared_ptr<DbEntity>> fetchAllRecords() {
        // проверка для команды SELECT так как она не использует executeQuery
        if (db == nullptr) {
            throw DbException("База данных не подключена! Пожалуйста, сначала нажмите 'Подключиться'.");
        }

        std::vector<std::shared_ptr<DbEntity>> recordsList;
        const char* sql = "SELECT id, name, age FROM persons;";
        sqlite3_stmt* statement;

        if (sqlite3_prepare_v2(db.get(), sql, -1, &statement, nullptr) != SQLITE_OK) {
            throw DbException("Ошибка подготовки команды SELECT");
        }

        while (sqlite3_step(statement) == SQLITE_ROW) {
            int id = sqlite3_column_int(statement, 0);
            
            const unsigned char* textPtr = sqlite3_column_text(statement, 1);
            std::string name = "";
            if (textPtr != nullptr) {
                name = reinterpret_cast<const char*>(textPtr);
            }
            
            int age = sqlite3_column_int(statement, 2);

            std::shared_ptr<DbEntity> entity = std::make_shared<PersonEntity>(id, name, age);
            recordsList.push_back(entity);
        }
        
        sqlite3_finalize(statement);
        return recordsList;
    }
};

// gui
class MainWindow : public Fl_Double_Window {
private:
    std::shared_ptr<DatabaseManager> dbManager;
    std::vector<std::shared_ptr<DbEntity>> currentData;

    Fl_Input* inputDbName;
    Fl_Button* btnOpen;
    Fl_Browser* browser;
    
    Fl_Input* inputName;
    Fl_Int_Input* inputAge;
    Fl_Button* btnAdd;
    Fl_Button* btnEdit;

    int selectedRecordId = -1; // id записи которую сейчас редактируем

public:
    MainWindow() : Fl_Double_Window(500, 420, "База Данных (SQLite)"), 
                   dbManager(std::make_shared<DatabaseManager>()) 
    {
        inputDbName = new Fl_Input(100, 10, 200, 25, "Файл БД:");
        inputDbName->value("test.db");
        
        btnOpen = new Fl_Button(310, 10, 180, 25, "Подключиться");
        btnOpen->callback(callbackOpenDb, this);

        browser = new Fl_Browser(10, 50, 480, 200);
        browser->type(FL_HOLD_BROWSER);
        browser->callback(callbackSelectRecord, this);

        inputName = new Fl_Input(100, 270, 390, 25, "Имя:");
        inputAge = new Fl_Int_Input(100, 310, 100, 25, "Возраст:");

        btnAdd = new Fl_Button(100, 350, 180, 40, "Добавить запись");
        btnAdd->callback(callbackAddRecord, this);

        btnEdit = new Fl_Button(310, 350, 180, 40, "Сохранить правки");
        btnEdit->callback(callbackEditRecord, this);
        btnEdit->deactivate(); // выключаем кнопку пока не выбрана запись

        end();
    }

private:
    void updateBrowserDisplay() {
        browser->clear();
        currentData.clear();
        
        try {
            currentData = dbManager->fetchAllRecords();
            for (size_t i = 0; i < currentData.size(); i++) {
                // вызовется getDisplayString из PersonEntity
                std::string displayText = currentData[i]->getDisplayString();
                browser->add(displayText.c_str());
            }
        } catch (const DbException& e) {
            fl_alert("Ошибка при обновлении списка: %s", e.what());
        }
    }

    // кнопка "подключиться"
    static void callbackOpenDb(Fl_Widget* widget, void* userData) {
        MainWindow* window = static_cast<MainWindow*>(userData);
        
        try {
            std::string fileName = window->inputDbName->value();
            window->dbManager->openOrCreate(fileName);
            fl_message("Успех: База данных подключена!");
            window->updateBrowserDisplay();
        } catch (const DbException& e) {
            fl_alert("Произошла ошибка: %s", e.what());
        }
    }

    // клик по списку (выбор записи)
    static void callbackSelectRecord(Fl_Widget* widget, void* userData) {
        MainWindow* window = static_cast<MainWindow*>(userData);
        int selectedIndex = window->browser->value();
        
        if (selectedIndex <= 0 || selectedIndex > window->currentData.size()) {
            return;
        }

        std::shared_ptr<DbEntity> basePointer = window->currentData[selectedIndex - 1];

	// DbEntity даункастится в PersonEntity
        std::shared_ptr<PersonEntity> personPointer = std::dynamic_pointer_cast<PersonEntity>(basePointer);
        
        if (personPointer != nullptr) {
            window->selectedRecordId = personPointer->getId();
            
            window->inputName->value(personPointer->getName().c_str());
            window->inputAge->value(std::to_string(personPointer->getAge()).c_str());
            
            window->btnEdit->activate(); 
            
            std::cout << "Пользователь выбрал: " << *personPointer << std::endl;
        }
    }

    // кнопка "добавить"
    static void callbackAddRecord(Fl_Widget* widget, void* userData) {
        MainWindow* window = static_cast<MainWindow*>(userData);
        std::string nameValue = window->inputName->value();
        std::string ageValue = window->inputAge->value();

        if (nameValue.empty() || ageValue.empty()) {
            fl_alert("Пожалуйста, заполните имя и возраст!");
            return;
        }

        try {
            int ageInt = std::stoi(ageValue);
            window->dbManager->insertRecord(nameValue, ageInt);
            
            window->updateBrowserDisplay();
            
            // очищаем поля ввода
            window->inputName->value("");
            window->inputAge->value("");
        } catch (const DbException& e) {
            fl_alert("Ошибка: %s", e.what());
        } catch (const std::exception& e) {
            fl_alert("Ошибка ввода: проверьте правильность возраста");
        }
    }

    // кнопка "сохранить правки"
    static void callbackEditRecord(Fl_Widget* widget, void* userData) {
        MainWindow* window = static_cast<MainWindow*>(userData);
        
        if (window->selectedRecordId == -1) {
            return; // запись не выбрана
        }

        std::string newName = window->inputName->value();
        std::string newAgeStr = window->inputAge->value();

        try {
            int newAgeInt = std::stoi(newAgeStr);
            window->dbManager->updateRecord(window->selectedRecordId, newName, newAgeInt);
            
            window->updateBrowserDisplay();
            
            window->btnEdit->deactivate();
            window->selectedRecordId = -1;
            window->inputName->value("");
            window->inputAge->value("");
        } catch (const DbException& e) {
            fl_alert("Ошибка: %s", e.what());
        } catch (const std::exception& e) {
            fl_alert("Неверный формат возраста!");
        }
    }
};

int main(int argc, char **argv) {
    std::unique_ptr<MainWindow> mainWindow = std::make_unique<MainWindow>();
    mainWindow->show(argc, argv);
    
    return Fl::run();
}
