use strict;
use warnings;
use Env;
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;
use File::Basename;

sub execute_regress_test {
    my $pub_node = $_[0];
    my $sub_node = $_[1];
    my $dbname = "postgres";
    my $dlpath    = dirname($ENV{REGRESS_SHLIB});
    my $inputdir = "../../regress";
    my $outputdir = $PostgreSQL::Test::Utils::tmp_check;

    # set up deparse testing resources
    create_deparse_testing_resources_on_pub_node($pub_node, $dbname);

    # execute core regression tests on pub node
    my $rc = system($ENV{PG_REGRESS}
        . " "
        . "--dlpath=\"$dlpath\" "
        . "--max-concurrent-tests=20 "
        . "--dbname="
        . $dbname . " "
        . "--use-existing "
        . "--host="
        . $pub_node->host . " "
        . "--port="
        . $pub_node->port . " "
        . "--inputdir=$inputdir "
        . "--outputdir=\"$outputdir\" "
        . "--schedule=$inputdir/parallel_schedule");
    if ($rc != 0)
    {
        # If regression test fails, dump out the regression diffs file
        my $diffs = "${outputdir}/regression/regression.diffs";
        if (-e $diffs)
        {
            print "=== dumping $diffs ===\n";
            print slurp_file($diffs);
            print "=== EOF ===\n";
        }
        die "Regression test failed";
    }

    # Retrieve SQL commands generated from deparsed DDLs on pub node
    my $ddl_sql = '';
    $pub_node -> psql($dbname,q(
        select ddl_deparse_expand_command(ddl) || ';' from deparsed_ddls ORDER BY id ASC),
        stdout => \$ddl_sql);

    # Execute SQL commands on sub node
    $sub_node -> psql($dbname, $ddl_sql);

    # Clean up deparse testing resources
    clean_deparse_testing_resources_on_pub_node($pub_node, $dbname);

    # Dump from pub node and sub node
    mkdir ${outputdir}."/dumps", 0755;
    my $pub_dump = ${outputdir}."/dumps/regress_pub.dump";
    my $sub_dump = ${outputdir}."/dumps/regress_sub.dump";
    system("pg_dumpall "
        . "-s "
        . "-f "
        . $pub_dump . " "
        . "--no-sync "
        .  '-p '
        . $pub_node->port)  == 0 or die "Dump pub node failed";
    system("pg_dumpall "
        . "-s "
        . "-f "
        . $sub_dump . " "
        . "--no-sync "
        .  '-p '
        . $sub_node->port)  == 0 or die "Dump sub node failed";

    # Compare dumped results
    is(system("diff "
    . $pub_dump . " "
    . $sub_dump), 0, "Comparing dumped output");

    # Close nodes
    $pub_node->stop;
    $sub_node->stop;
}

sub init_node {
    my $node_name = $_[0];
    my $node = PostgreSQL::Test::Cluster->new($node_name);
    $node->init;
    # increase some settings that Cluster->new makes too low by default.
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
    $node -> psql($dbname, q(
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
                insert into deparsed_ddls(tag, object_identity, ddl) values (r.object_type, r.object_identity, public.deparse_drop_ddl(r.object_identity, r.object_type));
            end loop;
        END;
        $$;

        create event trigger ddl_deparse_trig
        on ddl_command_end execute procedure deparse_to_json();

        create event trigger ddl_drops_deparse_trig
        on sql_drop execute procedure deparse_drops_to_json();

        commit;
    ));
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

# Create and start pub sub nodes
my $pub_node = init_pub_node("test");
my $sub_node = init_sub_node("test");
$pub_node -> start;
$sub_node -> start;

eval {execute_regress_test($pub_node, $sub_node);};
if ($@ ne "")
{
    fail($@);
}

# Close nodes
$pub_node->stop;
$sub_node->stop;

done_testing();