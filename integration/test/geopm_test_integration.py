#!/usr/bin/env python
#
#  Copyright (c) 2015, 2016, 2017, 2018, 2019, 2020, Intel Corporation
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in
#        the documentation and/or other materials provided with the
#        distribution.
#
#      * Neither the name of Intel Corporation nor the names of its
#        contributors may be used to endorse or promote products derived
#        from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

from __future__ import absolute_import

from __future__ import division
from future import standard_library
standard_library.install_aliases()
from builtins import str
from contextlib import contextmanager
import os
import sys
import unittest
import subprocess
import time
import pandas
import collections
import socket
import shlex
import json
import io
import time
import signal

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from integration.test import util
from integration.test import geopm_test_launcher
import geopmpy.io
import geopmpy.launcher
import geopmpy.hash

environment_default_path = os.path.join(util.get_config_value('GEOPM_CONFIG_PATH'), 'environment-default.json')
environment_override_path = os.path.join(util.get_config_value('GEOPM_CONFIG_PATH'), 'environment-override.json')


def create_frequency_map_policy(max_freq, frequency_map):
    """Create a frequency map to be consumed by the frequency map agent.

    Arguments:
    min_freq: Floor frequency for the agent
    max_freq: Ceiling frequency for the agent
    frequency_map: Dictionary mapping region names to frequencies
    """
    policy = {'FREQ_DEFAULT': max_freq, 'FREQ_UNCORE': float('nan')}
    for i, (region_name, frequency) in enumerate(frequency_map.items()):
        policy['HASH_{}'.format(i)] = geopmpy.hash.crc32_str(region_name)
        policy['FREQ_{}'.format(i)] = frequency

    return policy


class TestIntegration(unittest.TestCase):
    def setUp(self):
        self.longMessage = True
        self._agent = 'power_governor'
        self._options = {'power_budget': 150}
        self._tmp_files = []
        self._output = None
        self._power_limit = geopm_test_launcher.geopmread("MSR::PKG_POWER_LIMIT:PL1_POWER_LIMIT board 0")
        self._frequency = geopm_test_launcher.geopmread("MSR::PERF_CTL:FREQ board 0")
        self._original_freq_map_env = os.environ.get('GEOPM_FREQUENCY_MAP')

    def tearDown(self):
        geopm_test_launcher.geopmwrite("MSR::PKG_POWER_LIMIT:PL1_POWER_LIMIT board 0 " + str(self._power_limit))
        geopm_test_launcher.geopmwrite("MSR::PERF_CTL:FREQ board 0 " + str(self._frequency))
        if sys.exc_info() == (None, None, None) and os.getenv('GEOPM_KEEP_FILES') is None:
            if self._output is not None:
                self._output.remove_files()
            for ff in self._tmp_files:
                try:
                    os.remove(ff)
                except OSError:
                    pass
        if self._original_freq_map_env is None:
            if 'GEOPM_FREQUENCY_MAP' in os.environ:
                os.environ.pop('GEOPM_FREQUENCY_MAP')
        else:
            os.environ['GEOPM_FREQUENCY_MAP'] = self._original_freq_map_env

    def assert_geopm_uses_policy(self, expected_policy, context, user_policy=None):
        """Assert that geopm uses the given policy.

        Arguments:
        expected_policy (dict str->float): Policy to expect in generated reports.
        context (str): Additional context for test file names and failure messages.
        user_policy (dict str->float): Policy to request in the geopmlaunch command.
        """
        report_path = '{}.report'.format(context)
        num_node = 1
        num_rank = 1
        app_conf = geopmpy.io.BenchConf(context + '.app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', 0.01)
        user_agent_conf = geopmpy.io.AgentConf(context + '.user.agent.config', self._agent, {} if user_policy is None else user_policy)
        self._tmp_files.append(user_agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, user_agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(context, include_geopm_policy=user_policy is not None)
        self._tmp_files.append(report_path)

        actual_policy = geopmpy.io.RawReport(report_path).meta_data()['Policy']
        for expected_key, expected_value in expected_policy.items():
            self.assertEqual(expected_value, actual_policy[expected_key],
                             msg='Wrong policy value for {} (context: {})'.format(expected_key, context))
        for actual_key, actual_value in actual_policy.items():
            if actual_key not in expected_policy:
                self.assertEqual('NAN', actual_value,
                                 msg='Unexpected value for {} (context: {})'.format(actual_key, context))

    def create_progress_df(self, df):
        # Build a df with only the first region entry and the exit.
        df = df.reset_index(drop=True)
        last_index = 0
        filtered_df = pandas.DataFrame()
        row_list = []
        progress_1s = df['REGION_PROGRESS'].loc[df['REGION_PROGRESS'] == 1]
        for index, _ in progress_1s.iteritems():
            row = df.loc[last_index:index].head(1)
            row_list += [row[['TIME', 'REGION_PROGRESS', 'REGION_RUNTIME']]]
            row = df.loc[last_index:index].tail(1)
            row_list += [row[['TIME', 'REGION_PROGRESS', 'REGION_RUNTIME']]]
            last_index = index + 1  # Set the next starting index to be one past where we are
        filtered_df = pandas.concat(row_list)
        return filtered_df

    def test_report_and_trace_generation(self):
        name = 'test_report_and_trace_generation'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            report = self._output.get_report_data(node_name=nn)
            self.assertNotEqual(0, len(report))
            trace = self._output.get_trace_data(node_name=nn)
            self.assertNotEqual(0, len(trace))

    def test_report_and_trace_generation_pthread(self):
        name = 'test_report_and_trace_generation_pthread'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.set_pmpi_ctl('pthread')
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            report = self._output.get_report_data(node_name=nn)
            self.assertNotEqual(0, len(report))
            trace = self._output.get_trace_data(node_name=nn)
            self.assertNotEqual(0, len(trace))

    @unittest.skipUnless(geopm_test_launcher.detect_launcher() != "aprun",
                         'ALPS does not support multi-application launch on the same nodes.')
    @util.skip_unless_batch()
    def test_report_and_trace_generation_application(self):
        name = 'test_report_and_trace_generation_application'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.set_pmpi_ctl('application')
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            report = self._output.get_report_data(node_name=nn)
            self.assertNotEqual(0, len(report))
            trace = self._output.get_trace_data(node_name=nn)
            self.assertNotEqual(0, len(trace))

    @unittest.skipUnless(geopm_test_launcher.detect_launcher() == "srun" and os.getenv('SLURM_NODELIST') is None,
                         'Requires non-sbatch SLURM session for alloc\'d and idle nodes.')
    def test_report_generation_all_nodes(self):
        name = 'test_report_generation_all_nodes'
        report_path = name + '.report'
        num_node = 1
        num_rank = 1
        delay = 1.0
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', delay)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        time.sleep(5)  # Wait a moment to finish cleaning-up from a previous test
        idle_nodes = launcher.get_idle_nodes()
        idle_nodes_copy = list(idle_nodes)
        alloc_nodes = launcher.get_alloc_nodes()
        launcher.write_log(name, 'Idle nodes : {nodes}'.format(nodes=idle_nodes))
        launcher.write_log(name, 'Alloc\'d  nodes : {nodes}'.format(nodes=alloc_nodes))
        node_names = []
        for nn in idle_nodes_copy:
            launcher.set_node_list(nn.split())  # Hack to convert string to list
            try:
                launcher.run(name)
                node_names += nn.split()
            except subprocess.CalledProcessError as e:
                if e.returncode == 1 and nn not in launcher.get_idle_nodes():
                    launcher.write_log(name, '{node} has disappeared from the idle list!'.format(node=nn))
                    idle_nodes.remove(nn)
                else:
                    launcher.write_log(name, 'Return code = {code}'.format(code=e.returncode))
                    raise e
            ao = geopmpy.io.AppOutput(report_path, do_cache=False)
            sleep_data = ao.get_report_data(node_name=nn, region='sleep')
            app_data = ao.get_app_total_data(node_name=nn)
            self.assertNotEqual(0, len(sleep_data))
            util.assertNear(self, delay, sleep_data['runtime'].item())
            self.assertGreater(app_data['runtime'].item(), sleep_data['runtime'].item())
            self.assertEqual(1, sleep_data['count'].item())

        self.assertEqual(len(node_names), len(idle_nodes))

    def test_runtime(self):
        name = 'test_runtime'
        report_path = name + '.report'
        num_node = 1
        num_rank = 5
        delay = 3.0
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', delay)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path)
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            report = self._output.get_report_data(node_name=nn, region='sleep')
            app_total = self._output.get_app_total_data(node_name=nn)
            util.assertNear(self, delay, report['runtime'].item())
            self.assertGreater(app_total['runtime'].item(), report['runtime'].item())

    def test_runtime_epoch(self):
        name = 'test_runtime_epoch'
        report_path = name + '.report'
        num_node = 1
        num_rank = 5
        delay = 3.0
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', delay)
        app_conf.append_region('spin', delay)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path)
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            spin_data = self._output.get_report_data(node_name=nn, region='spin')
            sleep_data = self._output.get_report_data(node_name=nn, region='sleep')
            epoch_data = self._output.get_report_data(node_name=nn, region='epoch')
            total_runtime = sleep_data['runtime'].item() + spin_data['runtime'].item()
            util.assertNear(self, total_runtime, epoch_data['runtime'].item())

    def test_epoch_data_valid(self):
        name = 'test_epoch_data_valid'
        report_path = name + '.report'
        num_node = 1
        num_rank = 1
        big_o = 1.0
        loop_count = 10
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region('spin-unmarked', big_o)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        report = geopmpy.io.RawReport(report_path)
        node_names = report.host_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            regions = report.region_names(nn)
            self.assertTrue('model-init' not in regions)
            totals = report.raw_totals(nn)
            unmarked = report.raw_region(nn, 'unmarked-region')
            epoch = report.raw_epoch(nn)

            # Epoch has valid data
            self.assertGreater(epoch['runtime (sec)'], 0)
            self.assertGreater(epoch['sync-runtime (sec)'], 0)
            self.assertGreater(epoch['package-energy (joules)'], 0)
            self.assertGreater(epoch['dram-energy (joules)'], 0)
            self.assertGreater(epoch['power (watts)'], 0)
            self.assertGreater(epoch['frequency (%)'], 0)
            self.assertGreater(epoch['frequency (Hz)'], 0)
            self.assertEqual(epoch['count'], loop_count)

            for signal in ['runtime (sec)', 'package-energy (joules)', 'dram-energy (joules)']:
                util.assertNear(self, totals[signal], unmarked[signal], msg='signal={}'.format(signal))
                util.assertNear(self, totals[signal], epoch[signal], msg='signal={}'.format(signal))
                util.assertNear(self, totals[signal], epoch[signal], msg='signal={}'.format(signal))

            util.assertNear(self, unmarked['runtime (sec)'], unmarked['sync-runtime (sec)'])
            util.assertNear(self, epoch['runtime (sec)'], epoch['sync-runtime (sec)'])

    def test_runtime_nested(self):
        name = 'test_runtime_nested'
        report_path = name + '.report'
        num_node = 1
        num_rank = 1
        delay = 1.0
        loop_count = 2
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region('nested-progress', delay)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path)
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        for nn in node_names:
            spin_data = self._output.get_report_data(node_name=nn, region='spin')
            epoch_data = self._output.get_report_data(node_name=nn, region='epoch')
            app_totals = self._output.get_app_total_data(node_name=nn)
            # The spin sections of this region sleep for 'delay' seconds twice per loop.
            util.assertNear(self, 2 * loop_count * delay, spin_data['runtime'].item())
            util.assertNear(self, spin_data['runtime'].item(), epoch_data['runtime'].item(), epsilon=0.01)
            self.assertGreater(app_totals['network-time'].item(), 0)
            self.assertGreater(0.1, app_totals['network-time'].item())
            self.assertEqual(loop_count, spin_data['count'].item())

    def test_trace_runtimes(self):
        name = 'test_trace_runtimes'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', 1.0)
        app_conf.append_region('dgemm', 1.0)
        app_conf.append_region('all2all', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path,
                                                    trace_path, region_barrier=True)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        regions = self._output.get_region_names()
        for nn in node_names:
            trace = self._output.get_trace_data(node_name=nn)
            app_totals = self._output.get_app_total_data(node_name=nn)
            util.assertNear(self, trace.iloc[-1]['TIME'] - trace.iloc[0]['TIME'],
                            app_totals['runtime'].item(),
                            msg='Application runtime failure, node_name={}.'.format(nn))
            # Calculate runtime totals for each region in each trace, compare to report
            tt = trace.reset_index(level='index')  # move 'index' field from multiindex to columns
            tt = tt.set_index(['REGION_HASH'], append=True)  # add region_hash column to multiindex
            tt_reg = tt.groupby(level=['REGION_HASH'])
            for region_name in regions:
                region_data = self._output.get_report_data(node_name=nn, region=region_name)
                if (region_name not in ['unmarked-region', 'model-init', 'epoch'] and
                    not region_name.startswith('MPI_') and
                    region_data['sync_runtime'].item() != 0):
                    region_hash = region_data['id'].item()
                    trace_data = tt_reg.get_group(region_hash)
                    start_idx = trace_data.iloc[0]['index']
                    end_idx = trace_data.iloc[-1]['index'] + 1  # use time from sample after exiting region
                    start_time = tt.loc[tt['index'] == start_idx]['TIME'].item()
                    end_time = tt.loc[tt['index'] == end_idx]['TIME'].item()
                    trace_elapsed_time = end_time - start_time
                    msg = 'for region {rn} on node {nn}'.format(rn=region_name, nn=nn)
                    util.assertNear(self, trace_elapsed_time, region_data['sync_runtime'].item(), msg=msg)
            # epoch
            region_data = self._output.get_report_data(node_name=nn, region='epoch')
            trace_elapsed_time = trace.iloc[-1]['TIME'] - trace['TIME'].loc[trace['EPOCH_COUNT'] == 0].iloc[0]
            msg = 'for epoch on node {nn}'.format(nn=nn)
            util.assertNear(self, trace_elapsed_time, region_data['runtime'].item(), msg=msg)

    @util.skip_unless_config_enable('bloat')
    def test_runtime_regulator(self):
        name = 'test_runtime_regulator'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 1
        num_rank = 4
        loop_count = 20
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        sleep_big_o = 1.0
        spin_big_o = 0.5
        expected_region_runtime = {'spin': spin_big_o, 'sleep': sleep_big_o}
        app_conf.append_region('sleep', sleep_big_o)
        app_conf.append_region('spin', spin_big_o)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path, region_barrier=True)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        regions = self._output.get_region_names()
        for nn in node_names:
            app_totals = self._output.get_app_total_data(node_name=nn)
            trace = self._output.get_trace_data(node_name=nn)
            util.assertNear(self, trace.iloc[-1]['TIME'], app_totals['runtime'].item())
            tt = trace.set_index(['REGION_HASH'], append=True)
            tt = tt.groupby(level=['REGION_HASH'])
            for region_name in regions:
                region_data = self._output.get_report_data(node_name=nn, region=region_name)
                if region_name not in ['unmarked-region', 'model-init', 'epoch'] and not region_name.startswith('MPI_') and region_data['runtime'].item() != 0:
                    trace_data = tt.get_group(region_data['id'].item())
                    filtered_df = self.create_progress_df(trace_data)
                    first_time = False
                    epsilon = 0.001 if region_name != 'sleep' else 0.05
                    for index, df in filtered_df.iterrows():
                        if df['REGION_PROGRESS'] == 1:
                            util.assertNear(self, df['REGION_RUNTIME'], expected_region_runtime[region_name], epsilon=epsilon)
                            first_time = True
                        if first_time is True and df['REGION_PROGRESS'] == 0:
                            util.assertNear(self, df['REGION_RUNTIME'], expected_region_runtime[region_name], epsilon=epsilon)

    @util.skip_unless_run_long_tests()
    @util.skip_unless_config_enable('bloat')
    def test_region_runtimes(self):
        name = 'test_region_runtimes'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        loop_count = 500
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('dgemm', 8.0)
        app_conf.set_loop_count(loop_count)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path, time_limit=900)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)

        # Calculate region times from traces
        region_times = collections.defaultdict(lambda: collections.defaultdict(dict))
        for nn in node_names:
            tt = self._output.get_trace_data(node_name=nn).set_index(['REGION_HASH'], append=True).groupby(level=['REGION_HASH'])

            for region_hash, data in tt:
                filtered_df = self.create_progress_df(data)
                filtered_df = filtered_df.diff()
                # Since I'm not separating out the progress 0's from 1's, when I do the diff I only care about the
                # case where 1 - 0 = 1 for the progress column.
                filtered_df = filtered_df.loc[filtered_df['REGION_PROGRESS'] == 1]

                if len(filtered_df) > 1:
                    launcher.write_log(name, 'Region elapsed time stats from {} - {} :\n{}'\
                                       .format(nn, region_hash, filtered_df['TIME'].describe()))
                    filtered_df['TIME'].describe()
                    region_times[nn][region_hash] = filtered_df

            launcher.write_log(name, '{}'.format('-' * 80))

        # Loop through the reports to see if the region runtimes line up with what was calculated from the trace files above.
        regions = self._output.get_region_names()
        write_regions = True
        for nn in node_names:
            for region_name in regions:
                rr = self._output.get_report_data(node_name=nn, region=region_name)
                if (region_name != 'epoch' and
                    rr['id'].item() != 0 and
                    rr['count'].item() > 1):
                    if write_regions:
                        launcher.write_log(name, 'Region {} is {}.'.format(rr['id'].item(), region_name))
                    runtime = rr['sync_runtime'].item()
                    util.assertNear(self, runtime,
                                    region_times[nn][rr['id'].item()]['TIME'].sum())
            write_regions = False

        # Test to ensure every region detected in the trace is captured in the report.
        for nn in node_names:
            report_ids = []
            for region_name in regions:
                rr = self._output.get_report_data(node_name=nn, region=region_name)
                report_ids.append(rr['id'].item())
            for region_hash in region_times[nn].keys():
                self.assertTrue(region_hash in report_ids, msg='Report from {} missing region_hash {}'.format(nn, region_hash))

    def test_progress(self):
        name = 'test_progress'
        report_path = name + '.report'
        num_node = 1
        num_rank = 4
        delay = 3.0
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep-progress', delay)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path)
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        for nn in node_names:
            sleep_data = self._output.get_report_data(node_name=nn, region='sleep')
            app_total = self._output.get_app_total_data(node_name=nn)
            util.assertNear(self, delay, sleep_data['runtime'].item())
            self.assertGreater(app_total['runtime'].item(), sleep_data['runtime'].item())
            self.assertEqual(1, sleep_data['count'].item())

    def test_count(self):
        name = 'test_count'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 1
        num_rank = 4
        delay = 0.01
        loop_count = 100
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region('spin', delay)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        for nn in node_names:
            trace_data = self._output.get_trace_data(node_name=nn)
            spin_data = self._output.get_report_data(node_name=nn, region='spin')
            epoch_data = self._output.get_report_data(node_name=nn, region='epoch')
            util.assertNear(self, delay * loop_count, spin_data['runtime'].item())
            self.assertEqual(loop_count, spin_data['count'].item())
            self.assertEqual(loop_count, epoch_data['count'].item())
            self.assertEqual(loop_count, trace_data['EPOCH_COUNT'][-1])

    @util.skip_unless_run_long_tests()
    def test_scaling(self):
        """
        This test will start at ${num_node} nodes and ranks.  It will then calls check_run() to
        ensure that commands can be executed successfully on all of the allocated compute nodes.
        Afterwards it will run the specified app config on each node and verify the reports.  When
        complete it will double num_node and run the steps again.

        WARNING: This test can take a long time to run depending on the number of starting nodes and
        the size of the allocation.
        """
        name = 'test_scaling'
        report_path = name + '.report'
        num_node = 2
        loop_count = 100

        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('dgemm', 1.0)
        app_conf.append_region('all2all', 1.0)
        app_conf.set_loop_count(loop_count)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, time_limit=900)

        check_successful = True
        while check_successful:
            launcher.set_num_node(num_node)
            launcher.set_num_rank(num_node)
            try:
                launcher.check_run(name)
            except subprocess.CalledProcessError as e:
                # If we exceed the available nodes in the allocation ALPS/SLURM give a rc of 1
                # All other rc's are real errors
                if e.returncode != 1:
                    raise e
                check_successful = False
            if check_successful:
                launcher.write_log(name, 'About to run on {} nodes.'.format(num_node))
                launcher.run(name)
                self._output = geopmpy.io.AppOutput(report_path)
                node_names = self._output.get_node_names()
                self.assertEqual(len(node_names), num_node)
                for nn in node_names:
                    dgemm_data = self._output.get_report_data(node_name=nn, region='dgemm')
                    all2all_data = self._output.get_report_data(node_name=nn, region='all2all')
                    self.assertEqual(loop_count, dgemm_data['count'].item())
                    self.assertEqual(loop_count, all2all_data['count'].item())
                    self.assertGreater(dgemm_data['runtime'].item(), 0.0)
                    self.assertGreater(all2all_data['runtime'].item(), 0.0)
                num_node *= 2
                self._output.remove_files()

    @util.skip_unless_run_long_tests()
    def test_power_consumption(self):
        name = 'test_power_consumption'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        loop_count = 500
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('dgemm', 8.0)
        app_conf.set_loop_count(loop_count)

        fam, mod = geopm_test_launcher.get_platform()
        if fam == 6 and mod == 87:
            # budget for KNL
            self._options['power_budget'] = 130
        else:
            self._options['power_budget'] = 200
        gov_agent_conf_path = name + '_gov_agent.config'
        self._tmp_files.append(gov_agent_conf_path)
        gov_agent_conf = geopmpy.io.AgentConf(gov_agent_conf_path, self._agent, self._options)
        launcher = geopm_test_launcher.TestLauncher(app_conf, gov_agent_conf, report_path,
                                                    trace_path, time_limit=900)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.write_log(name, 'Power cap = {}W'.format(self._options['power_budget']))
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))
        all_power_data = {}
        # Total power consumed will be Socket(s) + DRAM
        for nn in node_names:
            tt = self._output.get_trace_data(node_name=nn)

            first_epoch_index = tt.loc[tt['EPOCH_COUNT'] == 0][:1].index[0]
            epoch_dropped_data = tt[first_epoch_index:]  # Drop all startup data

            power_data = epoch_dropped_data.filter(regex='ENERGY')
            power_data['TIME'] = epoch_dropped_data['TIME']
            power_data = power_data.diff().dropna()
            power_data.rename(columns={'TIME': 'ELAPSED_TIME'}, inplace=True)
            power_data = power_data.loc[(power_data != 0).all(axis=1)]  # Will drop any row that is all 0's

            pkg_energy_cols = [s for s in power_data.keys() if 'ENERGY_PACKAGE' in s]
            dram_energy_cols = [s for s in power_data.keys() if 'ENERGY_DRAM' in s]
            power_data['SOCKET_POWER'] = power_data[pkg_energy_cols].sum(axis=1) / power_data['ELAPSED_TIME']
            power_data['DRAM_POWER'] = power_data[dram_energy_cols].sum(axis=1) / power_data['ELAPSED_TIME']
            power_data['COMBINED_POWER'] = power_data['SOCKET_POWER'] + power_data['DRAM_POWER']

            pandas.set_option('display.width', 100)
            launcher.write_log(name, 'Power stats from {} :\n{}'.format(nn, power_data.describe()))

            all_power_data[nn] = power_data

        for node_name, power_data in all_power_data.items():
            # Allow for overages of 2% at the 75th percentile.
            self.assertGreater(self._options['power_budget'] * 1.02, power_data['SOCKET_POWER'].quantile(.75))

            # TODO Checks on the maximum power computed during the run?
            # TODO Checks to see how much power was left on the table?

    @util.skip_unless_run_long_tests()
    @util.skip_unless_batch()
    def test_power_balancer(self):
        name = 'test_power_balancer'
        num_node = 4
        num_rank = 16
        loop_count = 500
        # Require that the balancer moves the maximum dgemm runtime at
        # least 1/4 the distance to the mean dgemm runtime under the
        # governor.
        margin_factor =  0.25
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('dgemm-imbalance', 8.0)
        app_conf.append_region('all2all', 0.05)
        app_conf.set_loop_count(loop_count)

        # Update app config with imbalance
        alloc_nodes = geopm_test_launcher.TestLauncher.get_alloc_nodes()
        for nn in range(len(alloc_nodes) // 2):
            app_conf.append_imbalance(alloc_nodes[nn], 0.5)

        fam, mod = geopm_test_launcher.get_platform()
        if fam == 6 and mod == 87:
            # budget for KNL
            power_budget = 130
        else:
            power_budget = 200
        self._options = {'power_budget': power_budget}
        gov_agent_conf_path = name + '_gov_agent.config'
        bal_agent_conf_path = name + '_bal_agent.config'
        self._tmp_files.append(gov_agent_conf_path)
        self._tmp_files.append(bal_agent_conf_path)

        agent_list = ['power_governor', 'power_balancer']
        path_dict = {'power_governor': gov_agent_conf_path, 'power_balancer': bal_agent_conf_path}
        agent_runtime = dict()
        for agent in agent_list:
            agent_conf = geopmpy.io.AgentConf(path_dict[agent], agent, self._options)
            run_name = '{}_{}'.format(name, agent)
            report_path = '{}.report'.format(run_name)
            trace_path = '{}.trace'.format(run_name)
            launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path,
                                                        trace_path, time_limit=2700)
            launcher.set_num_node(num_node)
            launcher.set_num_rank(num_rank)
            launcher.write_log(run_name, 'Power cap = {}W'.format(power_budget))
            launcher.run(run_name)

            self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
            node_names = self._output.get_node_names()
            self.assertEqual(num_node, len(node_names))

            power_limits = []
            # Total power consumed will be Socket(s) + DRAM
            for nn in node_names:
                tt = self._output.get_trace_data(node_name=nn)

                first_epoch_index = tt.loc[tt['EPOCH_COUNT'] == 0][:1].index[0]
                epoch_dropped_data = tt[first_epoch_index:]  # Drop all startup data

                power_data = epoch_dropped_data.filter(regex='ENERGY')
                power_data['TIME'] = epoch_dropped_data['TIME']
                power_data = power_data.diff().dropna()
                power_data.rename(columns={'TIME': 'ELAPSED_TIME'}, inplace=True)
                power_data = power_data.loc[(power_data != 0).all(axis=1)]  # Will drop any row that is all 0's

                pkg_energy_cols = [s for s in power_data.keys() if 'ENERGY_PACKAGE' in s]
                dram_energy_cols = [s for s in power_data.keys() if 'ENERGY_DRAM' in s]
                power_data['SOCKET_POWER'] = power_data[pkg_energy_cols].sum(axis=1) / power_data['ELAPSED_TIME']
                power_data['DRAM_POWER'] = power_data[dram_energy_cols].sum(axis=1) / power_data['ELAPSED_TIME']
                power_data['COMBINED_POWER'] = power_data['SOCKET_POWER'] + power_data['DRAM_POWER']

                pandas.set_option('display.width', 100)
                launcher.write_log(name, 'Power stats from {} {} :\n{}'.format(agent, nn, power_data.describe()))

                # Get final power limit set on the node
                if agent == 'power_balancer':
                    power_limits.append(epoch_dropped_data['POWER_LIMIT'][-1])

            if agent == 'power_balancer':
                avg_power_limit = sum(power_limits) / len(power_limits)
                self.assertTrue(avg_power_limit <= power_budget)

            max_runtime = float('nan')
            node_names = self._output.get_node_names()
            runtime_list = []
            for node_name in node_names:
                epoch_data = self._output.get_report_data(node_name=node_name, region='dgemm')
                runtime_list.append(epoch_data['runtime'].item())
            if agent == 'power_governor':
                mean_runtime = sum(runtime_list) / len(runtime_list)
                max_runtime = max(runtime_list)
                margin = margin_factor * (max_runtime - mean_runtime)

            agent_runtime[agent] = max(runtime_list)

        self.assertGreater(agent_runtime['power_governor'] - margin,
                           agent_runtime['power_balancer'],
                           "governor runtime: {}, balancer runtime: {}, margin: {}".format(
                               agent_runtime['power_governor'], agent_runtime['power_balancer'], margin))

    def test_progress_exit(self):
        """
        Check that when we always see progress exit before the next entry.
        Make sure that progress only decreases when a new region is entered.
        """
        name = 'test_progress_exit'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 1
        num_rank = 16
        loop_count = 100
        big_o = 0.1
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region('dgemm-progress', big_o)
        app_conf.append_region('spin-progress', big_o)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path, region_barrier=True)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))

        for nn in node_names:
            tt = self._output.get_trace_data(node_name=nn)
            tt = tt.set_index(['REGION_HASH'], append=True)
            tt = tt.groupby(level=['REGION_HASH'])
            for region_hash, data in tt:
                tmp = data['REGION_PROGRESS'].diff()
                #@todo legacy branch?
                # Look for changes in progress that are more negative
                # than can be expected due to extrapolation error.
                if region_hash == 8300189175:
                    negative_progress = tmp.loc[(tmp > -1) & (tmp < -0.1)]
                    launcher.write_log(name, '{}'.format(negative_progress))
                    self.assertEqual(0, len(negative_progress))

    @util.skip_unless_run_long_tests()
    @util.skip_unless_optimized()
    def test_sample_rate(self):
        """
        Check that sample rate is regular and fast.
        """
        name = 'test_sample_rate'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 1
        num_rank = 16
        loop_count = 10
        big_o = 10.0
        region = 'dgemm-progress'
        max_mean = 0.01  # 10 millisecond max sample period
        max_nstd = 0.1  # 10% normalized standard deviation (std / mean)
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region(region, big_o)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(num_node, len(node_names))

        for nn in node_names:
            tt = self._output.get_trace_data(node_name=nn)
            delta_t = tt['TIME'].diff()
            delta_t = delta_t.loc[delta_t != 0]
            self.assertGreater(max_mean, delta_t.mean())
            # WARNING : The following line may mask issues in the sampling rate. To do a fine grained analysis, comment
            # out the next line and do NOT run on the BSP. This will require modifications to the launcher or manual testing.
            size_orig = len(delta_t)
            delta_t = delta_t[(delta_t - delta_t.mean()) < 3*delta_t.std()]  # Only keep samples within 3 stds of the mean
            self.assertGreater(0.06, 1 - (float(len(delta_t)) / size_orig))
            self.assertGreater(max_nstd, delta_t.std() / delta_t.mean())

    def test_network_times(self):
        name = 'test_network_times'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('sleep', 1.0)
        app_conf.append_region('dgemm', 1.0)
        app_conf.append_region('all2all', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        for nn in node_names:
            all2all_data = self._output.get_report_data(node_name=nn, region='all2all')
            sleep_data = self._output.get_report_data(node_name=nn, region='sleep')
            dgemm_data = self._output.get_report_data(node_name=nn, region='dgemm')
            barrier_data = self._output.get_report_data(node_name=nn, region='MPI_Barrier')
            unmarked_data = self._output.get_report_data(node_name=nn, region='unmarked-region')
            epoch_data = self._output.get_report_data(node_name=nn, region='epoch')
            app_total = self._output.get_app_total_data(node_name=nn)
            self.assertEqual(0, unmarked_data['count'].item())
            # Since MPI time is is counted if any rank on a node is in
            # an MPI call, but region time is counted only when all
            # ranks on a node are in a region, we must use the
            # unmarked-region time as our error term when comparing
            # MPI time and all2all time.
            mpi_epsilon = max(unmarked_data['runtime'].item() / all2all_data['network_time'].item(), 0.05)
            util.assertNear(self, all2all_data['network_time'].item(), all2all_data['runtime'].item(), mpi_epsilon)
            util.assertNear(self, all2all_data['network_time'].item() + barrier_data['network_time'].item(),
                            epoch_data['network_time'].item())
            # TODO: inconsistent; can we just use _ everywhere?
            util.assertNear(self, all2all_data['network_time'].item() + barrier_data['network_time'].item(),
                            app_total['network-time'].item())
            self.assertEqual(0, unmarked_data['network_time'].item())
            self.assertEqual(0, sleep_data['network_time'].item())
            self.assertEqual(0, dgemm_data['network_time'].item())

    def test_ignore_runtime(self):
        name = 'test_ignore_runtime'
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('ignore', 1.0)
        app_conf.append_region('dgemm', 1.0)
        app_conf.append_region('all2all', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        for nn in node_names:
            ignore_data = self._output.get_report_data(node_name=nn, region='ignore')
            startup_data = self._output.get_report_data(node_name=nn,
                                                        region='geopm_dgemm_model_region_startup')
            app_data = self._output.get_app_total_data(node_name=nn)
            util.assertNear(self, ignore_data['runtime'].item() + startup_data['runtime'].item(),
                            app_data['ignore-runtime'].item(), 0.00005)

    @util.skip_unless_config_enable('ompt')
    def test_unmarked_ompt(self):
        name = 'test_unmarked_ompt'
        report_path = name + '.report'
        num_node = 4
        num_rank = 16
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.append_region('stream-unmarked', 1.0)
        app_conf.append_region('dgemm-unmarked', 1.0)
        app_conf.append_region('all2all-unmarked', 1.0)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path)
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        stream_id = None
        region_names = self._output.get_region_names()
        stream_name = [key for key in region_names if key.lower().find('stream') != -1][0]
        for nn in node_names:
            stream_data = self._output.get_report_data(node_name=nn, region=stream_name)
            found = False
            for name in region_names:
                if stream_name in name:  # account for numbers at end of OMPT region names
                    found = True
            self.assertTrue(found)
            self.assertEqual(1, stream_data['count'].item())
            if stream_id:
                self.assertEqual(stream_id, stream_data['id'].item())
            else:
                stream_id = stream_data['id'].item()
            ompt_regions = [key for key in region_names if key.startswith('[OMPT]')]
            self.assertLessEqual(2, len(ompt_regions))
            self.assertTrue(('MPI_Alltoall' in region_names))
            gemm_region = [key for key in region_names if key.lower().find('gemm') != -1]
            self.assertLessEqual(1, len(gemm_region))

    def test_agent_frequency_map(self):
        name = 'test_agent_frequency_map'
        min_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_MIN board 0")
        max_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_MAX board 0")
        sticker_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_STICKER board 0")
        freq_step = geopm_test_launcher.geopmread("CPUINFO::FREQ_STEP board 0")
        self._agent = "frequency_map"
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 1
        num_rank = 4
        loop_count = 5
        dgemm_bigo = 15.0
        stream_bigo = 1.0
        dgemm_bigo_jlse = 35.647
        dgemm_bigo_quartz = 29.12
        stream_bigo_jlse = 1.6225
        stream_bigo_quartz = 1.7941
        hostname = socket.gethostname()
        if hostname.endswith('.alcf.anl.gov'):
            dgemm_bigo = dgemm_bigo_jlse
            stream_bigo = stream_bigo_jlse
        elif hostname.startswith('mcfly'):
            dgemm_bigo = 42.0
            stream_bigo = 1.75
        elif hostname.startswith('quartz'):
            dgemm_bigo = dgemm_bigo_quartz
            stream_bigo = stream_bigo_quartz

        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region('dgemm', dgemm_bigo)
        app_conf.append_region('stream', stream_bigo)
        app_conf.append_region('all2all', 1.0)
        app_conf.write()
        freq_map = {}
        freq_map['dgemm'] = min_freq + 2 * freq_step
        freq_map['stream'] = sticker_freq - 2 * freq_step
        freq_map['all2all'] = min_freq
        self._options = create_frequency_map_policy(max_freq, freq_map)
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path,
                                                    trace_path, region_barrier=True, time_limit=900)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)

        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        regions = self._output.get_region_names()
        for nn in node_names:
            for region_name in regions:
                region_data = self._output.get_report_data(node_name=nn, region=region_name)
                if (region_name in ['dgemm', 'stream', 'all2all']):
                    #todo verify trace frequencies
                    #todo verify agent report augment frequecies
                    msg = region_name + " frequency should be near assigned map frequency"
                    util.assertNear(self, freq_map[region_name] / sticker_freq * 100, region_data['frequency'].item(), msg=msg)

    def test_agent_energy_efficient_single_region(self):
        """
        Test of the EnergyEfficientAgent against single region loop.
        """
        name = 'test_energy_efficient_single_region'
        min_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_MIN board 0")
        sticker_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_STICKER board 0")
        self._agent = "energy_efficient"
        report_path = name + '.report'
        trace_path = name + '.trace'
        num_node = 1
        num_rank = 4
        loop_count = 100
        app_conf = geopmpy.io.BenchConf(name + '_app.config')
        self._tmp_files.append(app_conf.get_path())
        app_conf.set_loop_count(loop_count)
        app_conf.append_region('spin', 0.1)
        self._options = {'frequency_min': min_freq,
                         'frequency_max': sticker_freq}
        agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
        self._tmp_files.append(agent_conf.get_path())
        launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path, trace_path)
        launcher.set_num_node(num_node)
        launcher.set_num_rank(num_rank)
        launcher.run(name)
        self._output = geopmpy.io.AppOutput(report_path, trace_path + '*')
        node_names = self._output.get_node_names()
        self.assertEqual(len(node_names), num_node)
        regions = self._output.get_region_names()
        for nn in node_names:
            for region_name in regions:
                report = geopmpy.io.RawReport(report_path)
                if (region_name in ['spin']):
                    region = report.raw_region(nn, region_name)
                    msg = region_name + " frequency should be minimum frequency as specified by policy"
                    self.assertEqual(region['requested-online-frequency'], min_freq, msg=msg)  # freq should reduce

    @util.skip_unless_run_long_tests()
    @util.skip_unless_cpufreq()
    @util.skip_unless_batch()
    def test_agent_energy_efficient(self):
        """
        Test of the EnergyEfficientAgent.
        """
        name = 'test_energy_efficient_sticker'
        min_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_MIN board 0")
        sticker_freq = geopm_test_launcher.geopmread("CPUINFO::FREQ_STICKER board 0")
        self._agent = "energy_efficient"
        num_node = 1
        num_rank = 4
        loop_count = 200
        dgemm_bigo = 15.0
        stream_bigo = 1.0
        dgemm_bigo_jlse = 35.647
        dgemm_bigo_quartz = 29.12
        stream_bigo_jlse = 1.6225
        stream_bigo_quartz = 1.7941
        hostname = socket.gethostname()
        if hostname.endswith('.alcf.anl.gov'):
            dgemm_bigo = dgemm_bigo_jlse
            stream_bigo = stream_bigo_jlse
        elif hostname.startswith('mcfly'):
            dgemm_bigo = 42.0
            stream_bigo = 1.75
        elif hostname.startswith('quartz'):
            dgemm_bigo = dgemm_bigo_quartz
            stream_bigo = stream_bigo_quartz

        run = ['_sticker', '_nan_nan']
        for rr in run:
            report_path = name + rr + '.report'
            self._tmp_files.append(report_path)
            trace_path = name + rr + '.trace'
            app_conf = geopmpy.io.BenchConf(name + '_app.config')
            self._tmp_files.append(app_conf.get_path())
            app_conf.set_loop_count(loop_count)
            app_conf.append_region('dgemm', dgemm_bigo)
            app_conf.append_region('stream', stream_bigo)
            app_conf.write()
            if rr == '_sticker':
                self._options = {'frequency_min': sticker_freq,
                                 'frequency_max': sticker_freq}
            else:
                self._options = {'frequency_min': min_freq,
                                 'frequency_max': sticker_freq}
            agent_conf = geopmpy.io.AgentConf(name + '_agent.config', self._agent, self._options)
            self._tmp_files.append(agent_conf.get_path())
            launcher = geopm_test_launcher.TestLauncher(app_conf, agent_conf, report_path,
                                                        trace_path, region_barrier=True, time_limit=900)
            launcher.set_num_node(num_node)
            launcher.set_num_rank(num_rank)
            launcher.run(name + rr)

        # compare the app_total runtime and energy and assert within bounds
        report_path = name + run[0] + '.report'
        trace_path = name + run[0] + '.trace'
        sticker_out = geopmpy.io.AppOutput(report_path, trace_path + '*')
        for nn in sticker_out.get_node_names():
            self._tmp_files.append(trace_path + '-{}'.format(nn))
        report_path = name + run[1] + '.report'
        trace_path = name + run[1] + '.trace'
        nan_out = geopmpy.io.AppOutput(report_path, trace_path + '*')
        for nn in nan_out.get_node_names():
            self._tmp_files.append(trace_path + '-{}'.format(nn))
            sticker_app_total = sticker_out.get_app_total_data(node_name=nn)
            nan_app_total = nan_out.get_app_total_data(node_name=nn)
            runtime_savings_epoch = (sticker_app_total['runtime'].item() - nan_app_total['runtime'].item()) / sticker_app_total['runtime'].item()
            energy_savings_epoch = (sticker_app_total['energy-package'].item() - nan_app_total['energy-package'].item()) / sticker_app_total['energy-package'].item()
            self.assertLess(-0.1, runtime_savings_epoch)  # want -10% or better
            self.assertLess(0.0, energy_savings_epoch)

    @util.skip_unless_batch()
    @util.skip_or_ensure_writable_file(environment_default_path)
    @util.skip_or_ensure_writable_file(environment_override_path)
    @util.skip_unless_library_in_ldconfig('libgeopmpolicy.so')
    def test_geopm_environment(self):
        """Test behavior of geopm environment files.
        """
        def create_policy_file_on_compute_node(policy, policy_file_name, etc_config_path):
            policy_string = json.dumps(policy)
            policy_file_path = os.path.join('/tmp', policy_file_name)
            with open('/dev/null', 'w') as dev_null:
                util.run_script_on_compute_nodes(
                    'mv -f {policy_file} {policy_file}.backup ; '
                    'echo \'{policy}\' > {policy_file} && '
                    'echo \'{{"GEOPM_POLICY": "{policy_file}", "GEOPM_AGENT": "energy_efficient"}}\' > {etc_file}'.format(
                        policy=policy_string, policy_file=policy_file_path, etc_file=etc_config_path),
                    dev_null, dev_null)

        test_name = 'test_geopm_environment'
        self._agent = 'energy_efficient'
        user_policy = { 'PERF_MARGIN': 0.13 }

        with util.temporarily_remove_compute_node_file(environment_default_path), \
                util.temporarily_remove_compute_node_file(environment_override_path):
            # Only the default is set. Can be overridden by the user.
            default_policy = { 'PERF_MARGIN': 0.11 }
            create_policy_file_on_compute_node(default_policy, test_name + '.default.agent.config', environment_default_path)
            self.assert_geopm_uses_policy(default_policy, test_name + '_default_no_user')
            self.assert_geopm_uses_policy(user_policy, test_name + '_default_with_user', user_policy=user_policy)

            # Both default and override are set. Override is always used.
            override_policy = { 'PERF_MARGIN': 0.12 }
            create_policy_file_on_compute_node(override_policy, test_name + '.override.agent.config', environment_override_path)
            self.assert_geopm_uses_policy(override_policy, test_name + '_override_and_default_no_user')
            self.assert_geopm_uses_policy(override_policy, test_name + '_override_and_default_with_user', user_policy=user_policy)

            # Only override is set. Override is always used.
            util.remove_file_on_compute_nodes(environment_default_path)
            self.assert_geopm_uses_policy(override_policy, test_name + '_override_no_user')
            self.assert_geopm_uses_policy(override_policy, test_name + '_override_with_user', user_policy=user_policy)


class TestIntegrationGeopmio(unittest.TestCase):
    ''' Tests of geopmread and geopmwrite.'''
    def setUp(self):
        self.skip_warning_string = 'Incompatible CPU'

    def check_output(self, args, expected):
        try:
            proc = subprocess.Popen([self.exec_name] + args,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for exp in expected:
                line = proc.stdout.readline()
                while self.skip_warning_string.encode() in line:
                    line = proc.stdout.readline()
                self.assertIn(exp.encode(), line)
            for line in proc.stdout:
                if self.skip_warning_string.encode() not in line:
                    self.assertNotIn(b'Error', line)
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{} {}\n'.format(str(ex), stderr.getvalue()))

    def check_output_range(self, args, min_exp, max_exp):
        read_value = geopm_test_launcher.geopmread('{}'.format(' '.join(args)))

        self.assertLessEqual(min_exp, read_value, msg="Value read for {} smaller than {}: {}.".format(args, min_exp, read_value))
        self.assertGreaterEqual(max_exp, read_value, msg="Value read for {} larger than {}: {}.".format(args, max_exp, read_value))

    def check_no_error(self, args):
        stdout = io.StringIO()
        stderr = io.StringIO()
        test_exec = 'dummy -- {} {}'.format(self.exec_name, ' '.join(args))
        try:
            geopm_test_launcher.allocation_node_test(test_exec, stdout, stderr)
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{} {}\n'.format(str(ex), stderr.getvalue()))

        for line in stdout.getvalue().splitlines():
            if self.skip_warning_string not in line:
                self.assertNotIn('Error', line)

    def test_geopmread_command_line(self):
        '''
        Check that geopmread commandline arguments work.
        '''
        self.exec_name = "geopmread"

        # no args
        self.check_no_error([])

        # domain flag
        self.check_output(['--domain'], ['board', 'package', 'core', 'cpu',
                                         'board_memory', 'package_memory',
                                         'board_nic', 'package_nic',
                                         'board_accelerator', 'package_accelerator'])
        # read signal
        self.check_no_error(['TIME', 'board', '0'])

        # info
        self.check_no_error(['--info'])

        # errors
        read_err = 'domain type and domain index are required'
        self.check_output(['TIME'], [read_err])
        self.check_output(['TIME', 'board'], [read_err])
        self.check_output(['TIME', 'board', 'bad'], ['invalid domain index'])
        self.check_output(['FREQUENCY', 'package', '111'], ['cannot read signal'])
        self.check_output(['ENERGY_PACKAGE', 'cpu', '0'], ['cannot read signal'])
        self.check_output(['INVALID', 'board', '0'], ['cannot read signal'])
        self.check_output(['--domain', '--info'], ['info about domain not implemented'])

    @util.skip_unless_batch()
    def test_geopmread_all_signal_agg(self):
        '''
        Check that all reported signals can be read for board, aggregating if necessary.
        '''
        self.exec_name = "geopmread"
        stdout = io.StringIO()
        stderr = io.StringIO()
        test_exec = 'dummy -- {}'.format(self.exec_name)
        try:
            geopm_test_launcher.allocation_node_test(test_exec, stdout, stderr)
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{} {}\n'.format(str(ex), stderr.getvalue()))
            raise

        all_signals = []
        for line in stdout.getvalue().splitlines():
            if self.skip_warning_string not in line:
                all_signals.append(line.strip())
        for sig in all_signals:
            self.check_no_error([sig, 'board', '0'])

    @util.skip_unless_batch()
    def test_geopmread_signal_value(self):
        '''
        Check that some specific signals give a sane value.
        '''
        self.exec_name = "geopmread"
        signal_range = {
            "POWER_PACKAGE": (20, 400),
            "FREQUENCY": (1.0e8, 5.0e9),
            "TIME": (0, 10),  # time in sec to start geopmread
            "TEMPERATURE_CORE": (0, 100)
        }

        for signal, val_range in signal_range.items():
            try:
                self.check_no_error([signal, "board", "0"])
            except:
                raise
                pass  # skip missing signals
            else:
                self.check_output_range([signal, "board", "0"], *val_range)

    def test_geopmread_custom_msr(self):
        '''
        Check that MSRIOGroup picks up additional MSRs in path.
        '''
        self.exec_name = "geopmread"
        path = os.path.join(
           os.path.dirname(
            os.path.dirname(
             os.path.dirname(
              os.path.realpath(__file__)))),
           'examples/custom_msr/')
        custom_env = os.environ.copy()
        custom_env['GEOPM_PLUGIN_PATH'] = path
        all_signals = []
        try:
            proc = subprocess.Popen([self.exec_name], env=custom_env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for line in proc.stdout:
                if self.skip_warning_string.encode() not in line:
                    all_signals.append(line.strip())
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{}\n'.format(ex.output))
        self.assertIn(b'MSR::CORE_PERF_LIMIT_REASONS#', all_signals)

    def test_geopmwrite_command_line(self):
        '''
        Check that geopmwrite commandline arguments work.
        '''
        self.exec_name = "geopmwrite"

        # no args
        self.check_no_error([])

        # domain flag
        self.check_output(['--domain'], ['board', 'package', 'core', 'cpu',
                                         'board_memory', 'package_memory',
                                         'board_nic', 'package_nic',
                                         'board_accelerator', 'package_accelerator'])
        # errors
        write_err = 'domain type, domain index, and value are required'
        self.check_output(['FREQUENCY'], [write_err])
        self.check_output(['FREQUENCY', 'board'], [write_err])
        self.check_output(['FREQUENCY', 'board', '0'], [write_err])
        self.check_output(['FREQUENCY', 'board', 'bad', '0'], ['invalid domain index'])
        self.check_output(['FREQUENCY', 'board', '0', 'bad'], ['invalid write value'])
        self.check_output(['FREQUENCY', 'package', '111', '0'], ['cannot write control'])
        self.check_output(['FREQUENCY', 'board_nic', '0', '0'], ['cannot write control'])
        self.check_output(['INVALID', 'board', '0', '0'], ['cannot write control'])
        self.check_output(['--domain', '--info'], ['info about domain not implemented'])

    @util.skip_unless_batch()
    @util.skip_unless_stressng()
    def test_geopmwrite_set_freq(self):
        load_pid = None
        '''
        Check that geopmwrite can be used to set frequency.
        '''
        def read_current_freq(domain, signal='FREQUENCY'):
            return geopm_test_launcher.geopmread('{} {} {}'.format(signal, domain, '0'))

        def read_min_sticker_freq():
            return (geopm_test_launcher.geopmread('{} {} {}'.format('CPUINFO::FREQ_MIN', 'board', '0')),
                   geopm_test_launcher.geopmread('{} {} {}'.format('CPUINFO::FREQ_STICKER', 'board', '0')))

        def load_cpu_start():
            self.load_pid = subprocess.Popen('stress-ng --cpu=$(lscpu | grep -e "^CPU(" | cut -d: -f2 | tr -d " ")', shell=True)

        def load_cpu_stop():
            self.load_pid.send_signal(signal.SIGTERM)
            self.load_pid.communicate()

        @contextmanager
        def load_cpu():
            '''
            Context manager to put a load on every CPU on the node
            '''
            try:
                load_cpu_start()
                time.sleep(5)  # Give the load a moment to spin up
                yield
            finally:
                load_cpu_stop()

        read_domain = geopm_test_launcher.geopmread('--info CPU_FREQUENCY_STATUS')['domain']
        write_domain = geopm_test_launcher.geopmread('--info CPU_FREQUENCY_CONTROL')['domain']
        min_freq, sticker_freq = read_min_sticker_freq()

        old_freq = read_current_freq(write_domain, 'CPU_FREQUENCY_CONTROL')
        self.assertLess(old_freq, sticker_freq * 2)
        self.assertGreater(old_freq, min_freq - 1e8)

        with load_cpu():
            # Set to min and check
            geopm_test_launcher.geopmwrite('{} {} {} {}'.format('CPU_FREQUENCY_CONTROL', write_domain, '0', str(min_freq))),
            time.sleep(1)
            result = read_current_freq(read_domain)
            self.assertEqual(min_freq, result)
            # Set to sticker and check
            geopm_test_launcher.geopmwrite('{} {} {} {}'.format('CPU_FREQUENCY_CONTROL', write_domain, '0', str(sticker_freq))),
            time.sleep(1)
            result = read_current_freq(read_domain)
            self.assertEqual(sticker_freq, result)
            # Restore the original frequency
            geopm_test_launcher.geopmwrite('{} {} {} {}'.format('CPU_FREQUENCY_CONTROL', write_domain, '0', str(old_freq))),


class TestIntegrationGeopmagent(unittest.TestCase):
    ''' Tests of geopmagent.'''
    def setUp(self):
        self.exec_name = 'geopmagent'
        self.skip_warning_string = 'Incompatible CPU frequency driver/governor'

    def check_output(self, args, expected):
        try:
            proc = subprocess.Popen([self.exec_name] + args,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for exp in expected:
                line = proc.stdout.readline()
                while self.skip_warning_string.encode() in line or line == b'\n':
                    line = proc.stdout.readline()
                self.assertIn(exp.encode(), line)
            for line in proc.stdout:
                if self.skip_warning_string.encode() not in line:
                    self.assertNotIn(b'Error', line)
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{}\n'.format(ex.output))

    def check_json_output(self, args, expected):
        try:
            proc = subprocess.Popen([self.exec_name] + args,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{}\n'.format(ex.output))
        line = proc.stdout.readline()
        while self.skip_warning_string.encode() in line or line == b'\n':
            line = proc.stdout.readline()
        try:
            out_json = json.loads(line.decode())
        except ValueError:
            self.fail('Could not convert json string: {}\n'.format(line))
        self.assertEqual(expected, out_json)
        for line in proc.stdout:
            if self.skip_warning_string.encode() not in line:
                self.assertNotIn(b'Error', line)

    def check_no_error(self, args):
        try:
            proc = subprocess.Popen([self.exec_name] + args,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for line in proc.stdout:
                if self.skip_warning_string.encode() not in line:
                    self.assertNotIn(b'Error', line)
        except subprocess.CalledProcessError as ex:
            sys.stderr.write('{}\n'.format(ex.output))

    def test_geopmagent_command_line(self):
        '''
        Check that geopmagent commandline arguments work.
        '''
        # no args
        agent_names = ['monitor', 'power_balancer', 'power_governor',
                       'energy_efficient', 'frequency_map']
        self.check_output([], agent_names)

        # help message
        self.check_output(['--help'], ['Usage'])

        # version
        self.check_no_error(['--version'])

        # agent policy and sample names
        for agent in agent_names:
            self.check_output(['--agent', agent],
                              ['Policy', 'Sample'])

        # policy file
        self.check_json_output(['--agent', 'monitor', '--policy', 'None'],
                               {})
        self.check_json_output(['--agent', 'power_governor', '--policy', '150'],
                               {'POWER_PACKAGE_LIMIT_TOTAL': 150})
        # default value policy
        self.check_json_output(['--agent', 'power_governor', '--policy', 'NAN'],
                               {'POWER_PACKAGE_LIMIT_TOTAL': 'NAN'})
        self.check_json_output(['--agent', 'power_governor', '--policy', 'nan'],
                               {'POWER_PACKAGE_LIMIT_TOTAL': 'NAN'})
        self.check_json_output(['--agent', 'energy_efficient', '--policy', 'nan,nan'],
                               {'FREQ_MIN': 'NAN', 'FREQ_MAX': 'NAN'})
        self.check_json_output(['--agent', 'energy_efficient', '--policy', '1.2e9,nan'],
                               {'FREQ_MIN': 1.2e9, 'FREQ_MAX': 'NAN'})
        self.check_json_output(['--agent', 'energy_efficient', '--policy', 'nan,1.3e9'],
                               {'FREQ_MIN': 'NAN', 'FREQ_MAX': 1.3e9})
        # unspecified policy values are accepted
        self.check_json_output(['--agent', 'power_balancer', '--policy', '150'],
                               {'POWER_PACKAGE_LIMIT_TOTAL': 150})
        # hashing works for frequency map agent
        self.check_json_output(['--agent', 'frequency_map', '--policy', '1e9,nan,hello,2e9'],
                               {'FREQ_DEFAULT': 1e9, 'FREQ_UNCORE': 'NAN',
                                'HASH_0': geopmpy.hash.crc32_str('hello'), 'FREQ_0': 2e9})
        # errors
        self.check_output(['--agent', 'power_governor', '--policy', 'None'],
                          ['not a valid floating-point number', 'Invalid argument'])
        self.check_output(['--agent', 'monitor', '--policy', '300'],
                          ['agent takes no parameters', 'Invalid argument'])
        self.check_output(['--agent', 'energy_efficient', '--policy', '2.0e9,5.0e9,4.5e9,6.7,4.2'],
                          ['Number of policies', 'Invalid argument'])


if __name__ == '__main__':
    unittest.main()
