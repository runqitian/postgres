use strict;
use warnings;
use Env;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use File::Basename;

sub execute_test_case {
    my $test_name = $_[0];
    my $pub_node = $_[1];
    my $sub_node = $_[2];
    my $dbname = $_[3];
    my $user = $_[4];
    my $outputdir = $PostgreSQL::Test::Utils::tmp_check;

    # set up deparse testing resources
    create_deparse_testing_resources_on_pub_node($pub_node, $dbname, $user);

    my $test_file = "./sql/${test_name}.sql";
    my $content = do{local(@ARGV,$/)=$test_file;<>};
    my $pub_node_error = '';
    $pub_node -> psql($dbname, $content,
        stderr => \$pub_node_error,
        extra_params => ["-U", "${user}"]);
    # check execution of test SQL commands
    ok($pub_node_error eq '', "execute test SQL commands from ".$test_name) or diag("Failure from "
        .$test_file.": ".$pub_node_error);

    # retrieve reformed SQL commands on pub node, write to file
    my $ddl_sql = '';
    $pub_node -> psql($dbname,q(
        select ddl_deparse_expand_command(ddl) || ';' from deparsed_ddls ORDER BY id ASC),
        stdout => \$ddl_sql,
        extra_params => ["-U", "${user}"]);
    mkdir ${outputdir}."/ddl", 0755;
    my $ddl_output_file = ${outputdir}."/ddl/${test_name}.sql";
    open(FH, '>', $ddl_output_file) or die $!;
    print FH $ddl_sql;
    close(FH);

    # execute reformed SQL commands on sub node
    my $sub_node_error = '';
    $sub_node -> psql($dbname, $ddl_sql,
        stderr => \$sub_node_error,
        extra_params => ["-U", "${user}"]);
    # check execution of reformed DDL commands
    ok($sub_node_error eq '', "replay reformed DDL commands from ".$test_name) or diag("Failure from "
        .$ddl_output_file.": ".$sub_node_error);

    # clean up deparse testing resources
    clean_deparse_testing_resources_on_pub_node($pub_node, $dbname);
    # dump from pub node and sub node
    mkdir ${outputdir}."/dumps", 0755;
    my $pub_dump = ${outputdir}."/dumps/${test_name}_pub.dump";
    my $sub_dump = ${outputdir}."/dumps/${test_name}_sub.dump";
    my $dump_diff = ${outputdir}."/dumps/${test_name}_dump.diff";
    system("pg_dumpall "
        . "-s "
        . "-f "
        . $pub_dump . " "
        . "--no-sync "
        .  '-p '
        . $pub_node->port)  == 0 or die "Dump pub node failed in ${test_name}";
    system("pg_dumpall "
        . "-s "
        . "-f "
        . $sub_dump . " "
        . "--no-sync "
        .  '-p '
        . $sub_node->port)  == 0 or die "Dump sub node failed in ${test_name}";

    # compare dumped results
    system("diff ".$pub_dump." ".$sub_dump." > ".$dump_diff);
    ok(system("diff ".$pub_dump." ".$sub_dump) == 0, "compare dumped results in ${test_name}")
        or diag("Dumped results are different in ".$test_name
        .", check ".$dump_diff);
}

sub init_node {
    my $node_name = $_[0];
    my $node = PostgreSQL::Test::Cluster->new($node_name);
    # increase some settings that Cluster->new makes too low by default.
    $node -> init();
    $node -> start();
    $node -> append_conf('postgresql.conf', 'max_connections = 25');
    $node -> append_conf('postgresql.conf', 'client_min_messages = error');
    $node -> append_conf('postgresql.conf', 'max_prepared_transactions = 10');
    $node -> restart();
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
    my $user = $_[2];
    $node -> psql($dbname, "
        begin;
        CREATE EXTENSION test_ddl_deparse_regress;
        create table deparsed_ddls(id SERIAL PRIMARY KEY, tag text, object_identity text, ddl text);

        create or replace function deparse_to_json()
            returns event_trigger language plpgsql as
        \$\$
        declare
            r record;
        begin
            for r in select * from pg_event_trigger_ddl_commands()
            loop
                insert into deparsed_ddls(tag, object_identity, ddl) values (r.command_tag, r.object_identity, pg_catalog.ddl_deparse_to_json(r.command));
            end loop;
        END;
        \$\$;

        create or replace function deparse_drops_to_json()
            returns event_trigger language plpgsql as
        \$\$
        declare
            r record;
        begin
            for r in select * from pg_event_trigger_dropped_objects()
            loop
                insert into deparsed_ddls(tag, object_identity, ddl) values (r.object_type, r.object_identity, public.deparse_drop_ddl(r.object_identity, r.object_type));
            end loop;
        END;
        \$\$;

        create event trigger ddl_deparse_trig
        on ddl_command_end execute procedure deparse_to_json();

        create event trigger ddl_drops_deparse_trig
        on sql_drop execute procedure deparse_drops_to_json();

        commit;
    "
    );
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
        DROP EXTENSION test_ddl_deparse_regress;
    ));
}

sub create_prerequisite_resources {
    my $node = $_[0];
    my $dbname = $_[1];
    my $user = $_[2];
    $node -> safe_psql($dbname, "CREATE ROLE ${user} SUPERUSER LOGIN CREATEDB;");
}

sub trim {
    my @out = @_;
    for (@out) {
        s/^\s+//;
        s/\s+$//;
    }
    return wantarray ? @out : $out[0];
}

# Create and start pub sub nodes
my $initial_dbname = "postgres";
my $user = "ddl_testing_role";
my $pub_node = init_pub_node("test", $user);
my $sub_node = init_sub_node("test", $user);
create_prerequisite_resources($pub_node, $initial_dbname, $user);
create_prerequisite_resources($sub_node, $initial_dbname, $user);

# load test cases from the regression tests
my @regress_tests = split /\s+/, $ENV{REGRESS};
my $test_case_count = scalar @regress_tests - 1;
my $test_count_per_case = 3;
my $test_count = $test_case_count * $test_count_per_case;

plan tests => $test_count;
foreach(@regress_tests) {
    my $test_name = trim($_);
    # skip if it's regression test preparation or empty string
    if ($test_name eq "" or $test_name eq "test_ddl_deparse")
    {
        next;
    }
    my $test_dbname = $test_name;
    $pub_node -> safe_psql($initial_dbname, "CREATE DATABASE ${test_dbname};", extra_params => ["-U", "${user}"]);
    $sub_node -> safe_psql($initial_dbname, "CREATE DATABASE ${test_dbname};", extra_params => ["-U", "${user}"]);
    execute_test_case($test_name, $pub_node, $sub_node, $test_dbname, $user);
}
done_testing();

# Close nodes
$pub_node->stop;
$sub_node->stop;

