use strict;
use warnings;
use Env;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use File::Basename;

sub execute_regress_test {
    my $test_name = $_[0];
    my $dbname = "postgres";
    my $dlpath    = dirname($ENV{REGRESS_SHLIB});
    my $inputdir = ".";
    # I copied create_table.sql and comment the failed test case line 62-63, to show how this testing framework works
    # Change $inputdir to regres folder after deparser issue is fixed
    # my $inputdir = "../../regress";
    my $outputdir = $PostgreSQL::Test::Utils::tmp_check;
    my $pub_node = init_pub_node($test_name);
    my $sub_node = init_sub_node($test_name);

    $pub_node -> start;
    $sub_node -> start;

    create_deparse_testing_resources_on_pub_node($pub_node, $dbname);

    my $rc = system($ENV{PG_REGRESS}
        . " "
        . "--dlpath=\"$dlpath\" "
        . "--dbname="
        . $dbname . " "
        . "--use-existing "
        . "--host="
        . $pub_node->host . " "
        . "--port="
        . $pub_node->port . " "
        . "--inputdir=$inputdir "
        . "--outputdir=\"$outputdir\" "
        . $test_name);
    if ($rc != 0)
    {
        # Dump out the regression diffs file, if there is one
        my $diffs = "${outputdir}/regression/${test_name}.diffs";
        if (-e $diffs)
        {
            print "=== dumping $diffs ===\n";
            print slurp_file($diffs);
            print "=== EOF ===\n";
        }
    }
    is($rc, 0, "Execute regression test for ${test_name}");

    # Retrieve the deparsed DDLs.
    my $ddl_sql = '';
    is($pub_node -> psql(
        'postgres',
        q(select ddl_deparse_expand_command(ddl) || ';' from deparsed_ddls ORDER BY id ASC),
        stdout => \$ddl_sql), 0, 'Retrieve deparsed DDLs from pub node');
    
    $sub_node -> safe_psql('postgres', $ddl_sql);

    clean_deparse_testing_resources_on_pub_node($pub_node, $dbname);

    mkdir ${outputdir}."/dumps", 0755;
    my $pub_dump = ${outputdir}."/dumps/${test_name}_pub.dump";
    my $sub_dump = ${outputdir}."/dumps/${test_name}_sub.dump";
    command_ok(
        [
            'pg_dumpall', '-s', '-f', $pub_dump,
            '--no-sync', '-p', $pub_node->port
        ],
        'dump pub server');
    command_ok(
        [
            'pg_dumpall', '-s', '-f', $sub_dump,
            '--no-sync', '-p', $sub_node->port
        ],
        'dump sub server');
    command_ok(
        ['diff', $pub_dump, $sub_dump],
        'compare pub and sub dumps');

    $pub_node->stop;
    $sub_node->stop;
}

sub init_node {
    my $node_name = $_[0];
    my $node = PostgreSQL::Test::Cluster->new($node_name);
    $node->init;
    # Increase some settings that Cluster->new makes too low by default.
    $node->adjust_conf('postgresql.conf', 'max_connections', '25');
    $node->append_conf('postgresql.conf',
		   'max_prepared_transactions = 10');
    return $node;
}

sub init_pub_node {
    my $node_name = $_[0]."_pub";
    return init_node($node_name)
}

sub init_sub_node {
    my $node_name = $_[0]."_sub";
    return init_node($node_name)
}

sub create_deparse_testing_resources_on_pub_node {
    my $node = $_[0];
    my $dbname = $_[1];
    is($node -> psql($dbname, q(
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

        create or replace function deparse_drops_to_json()
            returns event_trigger language plpgsql as
        $$
        declare
            r record;
        begin
            for r in select * from pg_event_trigger_dropped_objects()
            loop
                insert into deparsed_ddls(tag, object_identity, ddl) values (r.object_type, r.object_identity, pg_catalog.deparse_drop_object(r.object_identity, r.object_type));
            end loop;
        END;
        $$;

        create event trigger ddl_deparse_trig
        on ddl_command_end execute procedure deparse_to_json();

        create event trigger ddl_drops_deparse_trig
        on sql_drop execute procedure deparse_drops_to_json();

        commit;
    )), 0, "Set up pub node for deparse testing");
}

sub clean_deparse_testing_resources_on_pub_node {
    my $node = $_[0];
    my $dbname = $_[1];
    # Drop the event trigger and the function before taking a logical dump.
    $node -> safe_psql($dbname,q(
        drop event trigger ddl_deparse_trig;
        drop event trigger ddl_drops_deparse_trig;
        drop function deparse_to_json();
        drop function deparse_drops_to_json();
        drop table deparsed_ddls;
    ));
}

sub trim {
    my @out = @_;
    for (@out) {
        s/^\s+//;
        s/\s+$//;
    }
    return wantarray ? @out : $out[0];
}

my $included_regress_tests_file = "./included_tests";
open(FH, $included_regress_tests_file) or die "File ${included_regress_tests_file} can not be opened";
while(<FH>)
{
    my $test_name = trim($_);
    execute_regress_test($test_name);
}
close;

done_testing();
