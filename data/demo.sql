# Demo script: ./build/minisql -f data/demo.sql
load users data/users.csv --schema data/users.schema.json
tables
schema users
SELECT * FROM users LIMIT 5
SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10;
SELECT name, age FROM users WHERE name = 'O''Brien'
EXPLAIN SELECT * FROM users WHERE id = 42
index users id
EXPLAIN SELECT * FROM users WHERE id = 42
SELECT * FROM users WHERE id = 12
index users city
EXPLAIN SELECT id, name FROM users WHERE age >= 21 AND city = 'Columbus' ORDER BY age DESC LIMIT 10
# Errors are reported, not fatal:
SELECT * FROM users WHERE age = '34'
SELECT * FROM users JOIN orders
