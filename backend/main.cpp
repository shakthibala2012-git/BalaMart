
#include <cstdlib>
#include <iostream>
#include <string>
#include <functional>

#include <drogon/drogon.h>
#include <libpq-fe.h>

using namespace drogon;

// Connect to PostgreSQL
PGconn* connectDB()
{
    const char* password = std::getenv("BALAMART_DB_PASSWORD");

    if (password == nullptr)
    {
        std::cerr << "BALAMART_DB_PASSWORD is not set." << std::endl;
        return nullptr;
    }

    const char* keywords[] = {
        "host", "port", "dbname", "user", "password", nullptr
    };

    const char* values[] = {
        "localhost", "5432", "BalaMart", "postgres", password, nullptr
    };

    PGconn* db = PQconnectdbParams(keywords, values, 0);

    if (PQstatus(db) != CONNECTION_OK)
    {
        std::cerr << "Database connection failed: "
                  << PQerrorMessage(db) << std::endl;

        PQfinish(db);
        return nullptr;
    }

    return db;
}

// Create a JSON response
HttpResponsePtr reply(Json::Value data, int status = 200)
{
    auto response = HttpResponse::newHttpJsonResponse(data);
    response->setStatusCode(static_cast<HttpStatusCode>(status));
    return response;
}

// Convert JSON numbers or strings to database-friendly strings
std::string jsonString(const Json::Value& value)
{
    if (value.isString())
        return value.asString();

    if (value.isNumeric())
        return value.asString();

    return "";
}

// Register a user
void registerUser(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& cb)
{
    auto body = req->getJsonObject();

    if (!body || !body->isMember("username") ||
        !body->isMember("password"))
    {
        Json::Value data;
        data["error"] = "Username and password are required.";
        cb(reply(data, 400));
        return;
    }

    std::string username = (*body)["username"].asString();
    std::string password = (*body)["password"].asString();

    if (username.empty() || password.size() < 6)
    {
        Json::Value data;
        data["error"] =
            "Enter a username and a password of at least 6 characters.";
        cb(reply(data, 400));
        return;
    }

    PGconn* db = connectDB();

    if (!db)
    {
        Json::Value data;
        data["error"] = "Database unavailable.";
        cb(reply(data, 500));
        return;
    }

    const char* params[] = {
        username.c_str(), password.c_str()
    };

    PGresult* result = PQexecParams(
        db,
        "INSERT INTO users (username, password) "
        "VALUES ($1, crypt($2, gen_salt('bf'))) "
        "RETURNING user_id",
        2, nullptr, params, nullptr, nullptr, 0
    );

    if (PQresultStatus(result) == PGRES_TUPLES_OK &&
        PQntuples(result) == 1)
    {
        Json::Value data;
        data["message"] = "Registration successful.";
        data["user_id"] = std::stoi(PQgetvalue(result, 0, 0));
        cb(reply(data, 201));
    }
    else
    {
        std::cerr << "Registration failed: "
                  << PQerrorMessage(db) << std::endl;

        Json::Value data;
        data["error"] =
            "Registration failed. The username may already exist, "
            "or database setup may be incomplete.";
        cb(reply(data, 400));
    }

    PQclear(result);
    PQfinish(db);
}

// Log in a user
void loginUser(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& cb)
{
    auto body = req->getJsonObject();

    if (!body || !body->isMember("username") ||
        !body->isMember("password"))
    {
        Json::Value data;
        data["error"] = "Username and password are required.";
        cb(reply(data, 400));
        return;
    }

    std::string username = (*body)["username"].asString();
    std::string password = (*body)["password"].asString();

    PGconn* db = connectDB();

    if (!db)
    {
        Json::Value data;
        data["error"] = "Database unavailable.";
        cb(reply(data, 500));
        return;
    }

    const char* params[] = {
        username.c_str(), password.c_str()
    };

    PGresult* result = PQexecParams(
        db,
        "SELECT user_id FROM users "
        "WHERE username = $1 AND password = crypt($2, password)",
        2, nullptr, params, nullptr, nullptr, 0
    );

    if (PQresultStatus(result) == PGRES_TUPLES_OK &&
        PQntuples(result) == 1)
    {
        Json::Value data;
        data["message"] = "Login successful.";
        data["user_id"] = std::stoi(PQgetvalue(result, 0, 0));
        data["username"] = username;
        cb(reply(data));
    }
    else
    {
        Json::Value data;
        data["error"] = "Invalid username or password.";
        cb(reply(data, 401));
    }

    PQclear(result);
    PQfinish(db);
}

// Get or add products
void productsHandler(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& cb)
{
    PGconn* db = connectDB();

    if (!db)
    {
        Json::Value data;
        data["error"] = "Database unavailable.";
        cb(reply(data, 500));
        return;
    }

    if (req->method() == Get)
    {
        PGresult* result = PQexec(
            db,
            "SELECT product_id, name, price, quantity "
            "FROM products ORDER BY product_id"
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::cerr << "Product query failed: "
                      << PQerrorMessage(db) << std::endl;

            Json::Value data;
            data["error"] =
                "Could not read products. Check the products table columns.";
            cb(reply(data, 500));
        }
        else
        {
            Json::Value products(Json::arrayValue);

            for (int i = 0; i < PQntuples(result); ++i)
            {
                Json::Value product;
                product["product_id"] =
                    std::stoi(PQgetvalue(result, i, 0));
                product["name"] = PQgetvalue(result, i, 1);
                product["price"] = std::stod(PQgetvalue(result, i, 2));
                product["quantity"] = std::stoi(PQgetvalue(result, i, 3));

                products.append(product);
            }

            cb(reply(products));
        }

        PQclear(result);
    }
    else if (req->method() == Post)
    {
        auto body = req->getJsonObject();

        if (!body || !body->isMember("name") ||
            !body->isMember("price") || !body->isMember("quantity"))
        {
            Json::Value data;
            data["error"] = "Provide name, price and quantity.";
            cb(reply(data, 400));
        }
        else
        {
            std::string name = (*body)["name"].asString();
            std::string price = jsonString((*body)["price"]);
            std::string quantity =
                std::to_string((*body)["quantity"].asInt());

            const char* params[] = {
                name.c_str(), price.c_str(), quantity.c_str()
            };

            PGresult* result = PQexecParams(
                db,
                "INSERT INTO products (name, price, quantity) "
                "VALUES ($1, $2, $3) RETURNING product_id",
                3, nullptr, params, nullptr, nullptr, 0
            );

            if (PQresultStatus(result) == PGRES_TUPLES_OK &&
                PQntuples(result) == 1)
            {
                Json::Value data;
                data["message"] = "Product added.";
                data["product_id"] =
                    std::stoi(PQgetvalue(result, 0, 0));
                cb(reply(data, 201));
            }
            else
            {
                std::cerr << "Add product failed: "
                          << PQerrorMessage(db) << std::endl;

                Json::Value data;
                data["error"] = "Could not add product.";
                cb(reply(data, 400));
            }

            PQclear(result);
        }
    }
    else
    {
        Json::Value data;
        data["error"] = "Method not supported.";
        cb(reply(data, 405));
    }

    PQfinish(db);
}

// Update or delete a product
void productByIdHandler(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& cb,
    int productId)
{
    PGconn* db = connectDB();

    if (!db)
    {
        Json::Value data;
        data["error"] = "Database unavailable.";
        cb(reply(data, 500));
        return;
    }

    std::string id = std::to_string(productId);
    PGresult* result = nullptr;

    if (req->method() == Put)
    {
        auto body = req->getJsonObject();

        if (!body || !body->isMember("name") ||
            !body->isMember("price") || !body->isMember("quantity"))
        {
            Json::Value data;
            data["error"] = "Provide name, price and quantity.";
            cb(reply(data, 400));
            PQfinish(db);
            return;
        }

        std::string name = (*body)["name"].asString();
        std::string price = jsonString((*body)["price"]);
        std::string quantity =
            std::to_string((*body)["quantity"].asInt());

        const char* params[] = {
            id.c_str(), name.c_str(), price.c_str(), quantity.c_str()
        };

        result = PQexecParams(
            db,
            "UPDATE products SET name=$2, price=$3, quantity=$4 "
            "WHERE product_id=$1 RETURNING product_id",
            4, nullptr, params, nullptr, nullptr, 0
        );
    }
    else if (req->method() == Delete)
    {
        const char* params[] = { id.c_str() };

        result = PQexecParams(
            db,
            "DELETE FROM products WHERE product_id=$1 "
            "RETURNING product_id",
            1, nullptr, params, nullptr, nullptr, 0
        );
    }

    if (!result)
    {
        Json::Value data;
        data["error"] = "Unsupported method.";
        cb(reply(data, 405));
    }
    else if (PQresultStatus(result) == PGRES_TUPLES_OK &&
             PQntuples(result) == 1)
    {
        Json::Value data;
        data["message"] =
            (req->method() == Put) ? "Product updated." : "Product deleted.";
        cb(reply(data));
    }
    else
    {
        std::cerr << "Product operation failed: "
                  << PQerrorMessage(db) << std::endl;

        Json::Value data;
        data["error"] =
            "Product not found or operation failed. "
            "A product with existing orders may not be deletable.";
        cb(reply(data, 400));
    }

    if (result)
        PQclear(result);

    PQfinish(db);
}

// Place an order and reduce stock atomically
void createOrder(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& cb)
{
    auto body = req->getJsonObject();

    if (!body || !body->isMember("user_id") ||
        !body->isMember("product_id") || !body->isMember("quantity"))
    {
        Json::Value data;
        data["error"] = "Provide user_id, product_id and quantity.";
        cb(reply(data, 400));
        return;
    }

    int userId = (*body)["user_id"].asInt();
    int productId = (*body)["product_id"].asInt();
    int quantity = (*body)["quantity"].asInt();

    if (userId <= 0 || productId <= 0 || quantity <= 0)
    {
        Json::Value data;
        data["error"] = "IDs and quantity must be positive.";
        cb(reply(data, 400));
        return;
    }

    PGconn* db = connectDB();

    if (!db)
    {
        Json::Value data;
        data["error"] = "Database unavailable.";
        cb(reply(data, 500));
        return;
    }

    // A single SQL statement updates stock and creates the order
    // only when sufficient stock exists.
    std::string userIdStr = std::to_string(userId);
    std::string productIdStr = std::to_string(productId);
    std::string quantityStr = std::to_string(quantity);

    const char* params[] = {
        userIdStr.c_str(), productIdStr.c_str(), quantityStr.c_str()
    };

    PGresult* result = PQexecParams(
        db,
        "WITH stock_update AS ("
        " UPDATE products "
        " SET quantity = quantity - $3::int "
        " WHERE product_id = $2::int AND quantity >= $3::int "
        " RETURNING product_id"
        ") "
        "INSERT INTO orders (user_id, product_id, quantity) "
        "SELECT $1::int, stock_update.product_id, $3::int "
        "FROM stock_update "
        "RETURNING order_id",
        3, nullptr, params, nullptr, nullptr, 0
    );

    if (PQresultStatus(result) == PGRES_TUPLES_OK &&
        PQntuples(result) == 1)
    {
        Json::Value data;
        data["message"] = "Order placed.";
        data["order_id"] = std::stoi(PQgetvalue(result, 0, 0));
        cb(reply(data, 201));
    }
    else
    {
        std::cerr << "Order failed: "
                  << PQerrorMessage(db) << std::endl;

        Json::Value data;
        data["error"] =
            "Order failed. Check the user, product and available stock.";
        cb(reply(data, 400));
    }

    PQclear(result);
    PQfinish(db);
}

int main()
{
    app().registerHandler(
        "/api/register",
        &registerUser,
        {Post}
    );

    app().registerHandler(
        "/api/login",
        &loginUser,
        {Post}
    );

    app().registerHandler(
        "/api/products",
        &productsHandler,
        {Get, Post}
    );

    app().registerHandler(
        "/api/products/{1}",
        &productByIdHandler,
        {Put, Delete}
    );

    app().registerHandler(
        "/api/orders",
        &createOrder,
        {Post}
    );

    app().setDocumentRoot("frontend")
        .addListener("127.0.0.1", 8080)
        .run();

    return 0;
}
