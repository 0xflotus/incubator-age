Utility Functions
=================

``create_graph()``
------------------

Creates a graph in the database.

Prototype
~~~~~~~~~

``create_graph(graph_name name) void``

Parameters
~~~~~~~~~~

+----------------+----------------------+
| Name           | Description          |
+================+======================+
| ``graph_name`` | The name of a graph. |
+----------------+----------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: postgresql

  =# SELECT create_graph('g');
  NOTICE:  graph "g" has been created
   create_graph
  --------------
  
  (1 row)

``drop_graph()``
----------------

Removes a graph from the database.

Prototype
~~~~~~~~~

``drop_graph(graph_name name, cascade bool = false) void``

Parameters
~~~~~~~~~~

+----------------+---------------------------------------------------------+
| Name           | Description                                             |
+================+=========================================================+
| ``graph_name`` | The name of a graph.                                    |
+----------------+---------------------------------------------------------+
| ``cascade``    | [optional] Automatically drop objects (labels, indexes, |
|                | etc.) that are contained in the graph.                  |
+----------------+---------------------------------------------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: postgresql

  =# SELECT drop_graph('g');
  NOTICE:  graph "g" has been dropped
   drop_graph
  ------------
  
  (1 row)

``alter_graph()``
-----------------

Alters a graph characteristic. Currently, the only operation supported is
``rename``.

Prototype
~~~~~~~~~

``alter_graph(graph_name name, operation cstring, new_value name) void``

Parameters
~~~~~~~~~~

+----------------+---------------------------------------------------------+
| Name           | Description                                             |
+================+=========================================================+
| ``graph_name`` | The name of the graph to modify. This parameter is case |
|                | sensitive.                                              |
+----------------+---------------------------------------------------------+
| ``operation``  | The name of the operation - see below. This parameter   |
|                | is case insensitive and needs to be in single quotes.   |
|                |                                                         |
|                | ``rename`` - renames ``graph_name`` to ``new_value``.   |
+----------------+---------------------------------------------------------+
| ``new_value``  | The new value. This parameter is case sensitive.        |
+----------------+---------------------------------------------------------+

Return Value
~~~~~~~~~~~~

N/A

Examples
~~~~~~~~

.. code-block:: postgresql

  =# SELECT alter_graph('Network', 'rename', 'lan_network');
  NOTICE:  graph "Network" renamed to "lan_network"
   alter_graph
  -------------
  
  (1 row)
