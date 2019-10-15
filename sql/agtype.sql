--
-- AGTYPE data type regression tests
--

--
-- Load extension and set path
--
LOAD 'agensgraph';
SET search_path TO ag_catalog;

--
-- Create a table using the AGTYPE type
--
CREATE TABLE agtype_table (type text, agtype agtype);

--
-- Insert values to exercise agtype_in/agtype_out
--
INSERT INTO agtype_table VALUES ('bool', 'true');
INSERT INTO agtype_table VALUES ('bool', 'false');

INSERT INTO agtype_table VALUES ('null', 'null');

INSERT INTO agtype_table VALUES ('string', '""');
INSERT INTO agtype_table VALUES ('string', '"This is a string"');

INSERT INTO agtype_table VALUES ('integer', '0');
INSERT INTO agtype_table VALUES ('integer', '9223372036854775807');
INSERT INTO agtype_table VALUES ('integer', '-9223372036854775808');

INSERT INTO agtype_table VALUES ('float', '0.0');
INSERT INTO agtype_table VALUES ('float', '1.0');
INSERT INTO agtype_table VALUES ('float', '-1.0');
INSERT INTO agtype_table VALUES ('float', '100000000.000001');
INSERT INTO agtype_table VALUES ('float', '-100000000.000001');
INSERT INTO agtype_table VALUES ('float', '0.00000000000000012345');
INSERT INTO agtype_table VALUES ('float', '-0.00000000000000012345');

INSERT INTO agtype_table VALUES ('integer array',
	'[-9223372036854775808, -1, 0, 1, 9223372036854775807]');
INSERT INTO agtype_table VALUES('float array',
	'[-0.00000000000000012345, -100000000.000001, -1.0, 0.0, 1.0, 100000000.000001, 0.00000000000000012345]');
INSERT INTO agtype_table VALUES('mixed array', '[true, false, null, "string", 1, 1.0, {"bool":true}]');

INSERT INTO agtype_table VALUES('object', '{"bool":true, "null":null, "string":"string", "integer":1, "float":1.2, "arrayi":[-1,0,1], "arrayf":[-1.0, 0.0, 1.0], "object":{"bool":true, "null":null, "string":"string", "int":1, "float":8.0}}');

--
-- Special float values: NaN, +/- Infinity
--
INSERT INTO agtype_table VALUES ('float  nan', 'nan');
INSERT INTO agtype_table VALUES ('float  Infinity', 'Infinity');
INSERT INTO agtype_table VALUES ('float -Infinity', '-Infinity');
INSERT INTO agtype_table VALUES ('float  inf', 'inf');
INSERT INTO agtype_table VALUES ('float -inf', '-inf');

SELECT * FROM agtype_table;

--
-- These should fail
--
INSERT INTO agtype_table VALUES ('bad integer', '9223372036854775808');
INSERT INTO agtype_table VALUES ('bad integer', '-9223372036854775809');
INSERT INTO agtype_table VALUES ('bad float', '-NaN');
INSERT INTO agtype_table VALUES ('bad float', 'Infi');
INSERT INTO agtype_table VALUES ('bad float', '-Infi');

--
-- Test agtype mathematical operator functions
-- +, -, unary -, *, /, %, and ^
--
SELECT agtype_add('1', '-1');
SELECT agtype_add('1', '-1.0');
SELECT agtype_add('1.0', '-1');
SELECT agtype_add('1.0', '-1.0');

SELECT agtype_sub('-1', '-1');
SELECT agtype_sub('-1', '-1.0');
SELECT agtype_sub('-1.0', '-1');
SELECT agtype_sub('-1.0', '-1.0');

SELECT agtype_neg('-1');
SELECT agtype_neg('-1.0');
SELECT agtype_neg('0');
SELECT agtype_neg('0.0');

SELECT agtype_mul('-2', '3');
SELECT agtype_mul('2', '-3.0');
SELECT agtype_mul('-2.0', '3');
SELECT agtype_mul('2.0', '-3.0');

SELECT agtype_div('-4', '3');
SELECT agtype_div('4', '-3.0');
SELECT agtype_div('-4.0', '3');
SELECT agtype_div('4.0', '-3.0');

SELECT agtype_pow('-2', '3');
SELECT agtype_pow('2', '-1.0');
SELECT agtype_pow('2.0', '3');
SELECT agtype_pow('2.0', '-1.0');

--
-- Should fail with divide by zero
--
SELECT agtype_div('1', '0');
SELECT agtype_div('1', '0.0');
SELECT agtype_div('1.0', '0');
SELECT agtype_div('1.0', '0.0');

--
-- Should get Infinity
--
SELECT agtype_pow('0', '-1');
SELECT agtype_pow('-0.0', '-1');

--
-- Test operators +, -, unary -, *, /, %, and ^
--
SELECT '3.14'::agtype + '3.14'::agtype;
SELECT '3.14'::agtype - '3.14'::agtype;
SELECT -'3.14'::agtype;
SELECT '3.14'::agtype * '3.14'::agtype;
SELECT '3.14'::agtype / '3.14'::agtype;
SELECT '3.14'::agtype % '3.14'::agtype;
SELECT '3.14'::agtype ^ '2'::agtype;

--
-- Test orderability of comparison operators =, <>, <, >, <=, >=
-- These should all return true
-- Integer
SELECT agtype_in('1') = agtype_in('1');
SELECT agtype_in('1') <> agtype_in('2');
SELECT agtype_in('1') <> agtype_in('-2');
SELECT agtype_in('1') < agtype_in('2');
SELECT agtype_in('1') > agtype_in('-2');
SELECT agtype_in('1') <= agtype_in('2');
SELECT agtype_in('1') >= agtype_in('-2');

-- Float
SELECT agtype_in('1.01') = agtype_in('1.01');
SELECT agtype_in('1.01') <> agtype_in('1.001');
SELECT agtype_in('1.01') <> agtype_in('1.011');
SELECT agtype_in('1.01') < agtype_in('1.011');
SELECT agtype_in('1.01') > agtype_in('1.001');
SELECT agtype_in('1.01') <= agtype_in('1.011');
SELECT agtype_in('1.01') >= agtype_in('1.001');
SELECT agtype_in('1.01') < agtype_in('Infinity');
SELECT agtype_in('1.01') > agtype_in('-Infinity');
-- NaN, under ordering, is considered to be the biggest numeric value
-- greater than positive infinity. So, greater than any other number.
SELECT agtype_in('1.01') < agtype_in('NaN');
SELECT agtype_in('NaN') > agtype_in('Infinity');
SELECT agtype_in('NaN') > agtype_in('-Infinity');
SELECT agtype_in('NaN') = agtype_in('NaN');

-- Mixed Integer and Float
SELECT agtype_in('1') = agtype_in('1.0');
SELECT agtype_in('1') <> agtype_in('1.001');
SELECT agtype_in('1') <> agtype_in('0.999999');
SELECT agtype_in('1') < agtype_in('1.001');
SELECT agtype_in('1') > agtype_in('0.999999');
SELECT agtype_in('1') <= agtype_in('1.001');
SELECT agtype_in('1') >= agtype_in('0.999999');
SELECT agtype_in('1') < agtype_in('Infinity');
SELECT agtype_in('1') > agtype_in('-Infinity');
SELECT agtype_in('1') < agtype_in('NaN');

-- Mixed Float and Integer
SELECT agtype_in('1.0') = agtype_in('1');
SELECT agtype_in('1.001') <> agtype_in('1');
SELECT agtype_in('0.999999') <> agtype_in('1');
SELECT agtype_in('1.001') > agtype_in('1');
SELECT agtype_in('0.999999') < agtype_in('1');

-- Strings
SELECT agtype_in('"a"') = agtype_in('"a"');
SELECT agtype_in('"a"') <> agtype_in('"b"');
SELECT agtype_in('"a"') < agtype_in('"aa"');
SELECT agtype_in('"b"') > agtype_in('"aa"');
SELECT agtype_in('"a"') <= agtype_in('"aa"');
SELECT agtype_in('"b"') >= agtype_in('"aa"');

-- Lists
SELECT agtype_in('[0, 1, null, 2]') = agtype_in('[0, 1, null, 2]');
SELECT agtype_in('[0, 1, null, 2]') <> agtype_in('[2, null, 1, 0]');
SELECT agtype_in('[0, 1, null]') < agtype_in('[0, 1, null, 2]');
SELECT agtype_in('[1, 1, null, 2]') > agtype_in('[0, 1, null, 2]');

-- Objects (Maps)
SELECT agtype_in('{"bool":true, "null": null}') = agtype_in('{"null":null, "bool":true}');
SELECT agtype_in('{"bool":true}') < agtype_in('{"bool":true, "null": null}');

-- Comparisons between types
-- Object < List < String < Boolean < Integer = Float = Numeric < Null
SELECT agtype_in('1') < agtype_in('null');
SELECT agtype_in('NaN') < agtype_in('null');
SELECT agtype_in('Infinity') < agtype_in('null');
SELECT agtype_in('true') < agtype_in('1');
SELECT agtype_in('true') < agtype_in('NaN');
SELECT agtype_in('true') < agtype_in('Infinity');
SELECT agtype_in('"string"') < agtype_in('true');
SELECT agtype_in('[1,3,5,7,9,11]') < agtype_in('"string"');
SELECT agtype_in('{"bool":true, "integer":1}') < agtype_in('[1,3,5,7,9,11]');
SELECT agtype_in('[1, "string"]') < agtype_in('[1, 1]');
SELECT agtype_in('{"bool":true, "integer":1}') < agtype_in('{"bool":true, "integer":null}');

--
-- Cleanup
--
DROP TABLE agtype_table;

--
-- End of AGTYPE data type regression tests
--
