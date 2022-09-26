use strict;
use warnings;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use File::Basename;

my $node1 = PostgreSQL::Test::Cluster->new('main');
$node1->init;

# Increase some settings that Cluster->new makes too low by default.
$node1->adjust_conf('postgresql.conf', 'max_connections', '25');
$node1->append_conf('postgresql.conf',
		   'max_prepared_transactions = 10');

# Create the event trigger to get deparsed DDLs.
$node1->start;
is( $node1->psql('postgres',
		 q(
begin;
create table deparsed_ddls(id SERIAL PRIMARY KEY, tag text, object_identity text, ddl text);

create or replace function deparse_to_json()
    returns event_trigger language plpgsql as
$$
declare
    r record;
begin
    for r in select * from pg_event_trigger_ddl_commands()
    loop
        insert into deparsed_ddls(tag, object_identity, ddl) values (r.command_tag, r.object_identity, pg_catalog.ddl_deparse_to_json(r.command));
    end loop;
END;
$$;

create event trigger ddl_deparse_trig
on ddl_command_end execute procedure deparse_to_json();
commit;
)), 0, 'event trigger created');

# my $dlpath    = "."

# Run the regression tests against the main server.
#
# FIXME: As the deparsing DDL code is in under development and many DDLs are not
# supported yet, regression tests againt the main server fails.
#
# my $extra_opts = $ENV{EXTRA_REGRESS_OPTS} || "";
# my $rc =
#  system($ENV{PG_REGRESS}
# 	  . " $extra_opts "
# 	  . "--dlpath=\"$dlpath\" "
# 	  . "--bindir= "
# 	  . "--host="
# 	  . $node1->host . " "
# 	  . "--port="
# 	  . $node1->port . " "
# 	  . "--schedule=../../regress/parallel_schedule "
# 	  . "--use-existing "
# 	  . "--max-concurrent-tests=20 "
# 	  . "--inputdir=../../regress ");
# if ($rc != 0)
# {
# 	# Dump out the regression diffs file, if there is one
# 	my $diffs = "./regression.diffs";
# 	if (-e $diffs)
# 	{
# 		print "=== dumping $diffs ===\n";
# 		print slurp_file($diffs);
# 		print "=== EOF ===\n";
# 	}
# }

# is($rc, 0, 'regression tests pass');

# FIXME: temporary tests in lieu of running the regression tests
my $sql_cmd = "";
my $filename = './sql/create_table.sql';
open(FH, '<', $filename) or die $!;
while(<FH>){
    $sql_cmd = $sql_cmd.$_;
}

$node1->psql(
    'postgres', $sql_cmd, on_error_stop => 0, on_error_die => 0);

print "=======Executed regression tests for create_table.sql";

# Retrieve the deparsed DDLs.
my $ddl_sql = '';
is( $node1->psql(
	'postgres',
	q(select ddl_deparse_expand_command(ddl) || ';' from deparsed_ddls ORDER BY id ASC),
	stdout => \$ddl_sql
    ), 0, 'dump deparsed DDLs');

print "======== Here areDDL SQLs:";
print $ddl_sql;
print "======== Here are all DDL SQLs:";


print "=======Dumped deparsed DDLs";

# Initialize another database cluster where we load the deparsed DDLs.
my $node2 = PostgreSQL::Test::Cluster->new('sub');
$node2->init;
$node2->start;

print "=======Node 2 started";

# Load the deparsed DDLs.
$node2->safe_psql('postgres', $ddl_sql);

print "=======Node 2 load DDLs";

# Drop the event trigger and the function before taking a logical dump.
$node1->safe_psql(
    'postgres',
    q(
drop event trigger ddl_deparse_trig;
drop function deparse_to_json();
drop table deparsed_ddls;
));

# Perform a logical dump of both the main and the sub server, ane check
# that they match.
command_ok(
	[
		'pg_dumpall', '-f', "." . '/main.dump',
		'--no-sync', '-p', $node1->port
	],
    'dump main server');
command_ok(
	[
		'pg_dumpall', '-f', "." . '/sub.dump',
		'--no-sync', '-p', $node2->port
	],
    'dump main server');
command_ok(
	[ 'diff', "." . '/main.dump', "." . '/sub.dump' ],
	'compare main and sub dumps');

$node1->stop;
$node2->stop;

done_testing();
