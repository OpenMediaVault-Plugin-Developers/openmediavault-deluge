<?php

/**
 * Copyright (C) 2010-2012 Ian Moore <imooreyahoo@gmail.com>
 * Copyright (C) 2013-2015 OpenMediaVault Plugin Developers.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 //

namespace OMV\Engined\Rpc;

use DateTime;
use FilesystemIterator;
use Exception;
use OMV\Config\Database;
use OMV\Engine\Notify\Dispatcher;
use OMV\Rpc\ServiceAbstract;
use OMV\Rpc\ServiceManager as RpcServiceManager;
use OMV\System\Process;
use SplFileObject;
use Phar;
use PharData;
use RecursiveDirectoryIterator;
use RecursiveIteratorIterator;

class Deluge extends ServiceAbstract
{
    private $database;

    private $dataModelPath = 'conf.service.deluge';

    private $dispatcher;

    private $eventMessagePath = 'org.openmediavault.conf.services.deluge';

    private $delugeSettingsFile = '/home/deluge-daemon/.config/deluge/web.conf';
    private $delugeSettingsDir = '/home/deluge-daemon/.config/deluge';

    public function __construct(Database $database = null, Dispatcher $dispatcher = null)
    {
        $this->database = $database ?: Database::getInstance();
        $this->dispatcher = $dispatcher ?: Dispatcher::getInstance();
    }

    public function getName()
    {
        return 'Deluge';
    }

    public function initialize()
    {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");

        $this->registerMethod("downloadBackup");
        $this->registerMethod("uploadBackup");
        $this->registerMethod("doInstall");
        $this->registerMethod("doOld");;
        $this->registerMethod("doReset");
    }

    public function getSettings($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $object = $this->database->get($this->dataModelPath);

        $port = 8112;

        // Get current port as set in Deluge config file
        if (file_exists($this->delugeSettingsFile)) {
            $config = parse_ini_file(
                $this->delugeSettingsFile,
                true,
                INI_SCANNER_RAW
            );

            if (!empty($config["port"])) {
                $port = $config["port"];
            }
        }

        $object->add('port', 'integer');
        $object->set('port', intval($port));

        return $object->getAssoc();
    }

    public function setSettings($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, 'rpc.deluge.setsettings');

        $object = $this->database->get($this->dataModelPath);

        $object->setAssoc($params);

        $this->database->set($object);
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath, $object);

        return $object->getAssoc();
    }

    public function downloadBackup($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        $db = \OMV\Config\Database::getInstance();

        if (!file_exists($this->delugeSettingsDir)) {
            throw new Exception('Nothing to backup found.');
        }

        $tmpFile = sprintf('%s/%s.tar', sys_get_temp_dir(), uniqid());
        $archive = new PharData($tmpFile);

        //$archive->addFile($settings['db-folder'], 'settings.conf');

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                $this->delugeSettingsDir,
                FilesystemIterator::SKIP_DOTS
            )
        );

        $archive->buildFromIterator($directoryIterator, $this->delugeSettingsDir);
        $archive->compress(Phar::GZ);

        // Remove uncompressed tar and set $tmpFile to the compressed.
        unlink($tmpFile);
        $tmpFile = sprintf('%s.gz', $tmpFile);

        $now = new DateTime();
        $time = $now->format(DateTime::ISO8601);

        return [
            'contenttype' => 'application/x-tar',
            'filename' => sprintf('deluge-%s.tar.gz', $time),
            'filepath' => $tmpFile,
        ];
    }

    public function uploadBackup($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);
        $this->validateMethodParams($params, 'rpc.plugin.upload');

        $db = \OMV\Config\Database::getInstance();
        $settings = $db->getAssoc($this->dataModelPath);

        // Rename file so that it's recognized by the PharData class.
        $filepath = sprintf('%s.tar.gz', $params['filepath']);
        rename($params['filepath'], $filepath);

        $directoryIterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator(
                $this->delugeSettingsDir,
                FilesystemIterator::SKIP_DOTS
            ),
            RecursiveIteratorIterator::CHILD_FIRST
        );

        foreach ($directoryIterator as $file) {
            if ($file->isFile()) {
                unlink($file->getPathname());
            }

            if ($file->isDir()) {
                rmdir($file->getPathname());
            }
        }

        $archive = new PharData($filepath);
        $archive->extractTo($this->delugeSettingsDir);

        foreach ($directoryIterator as $file) {
            chown($file->getPathname(), $settings['user']);
            chgrp($file->getPathname(), $settings['user']);
        }

        // Rename file back to orignal name so that OMV will remove it.
        rename($filepath, $params['filepath']);

        // Send a modify signal so that the user can trigger a restart.
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath);
    }

    public function doInstall($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Create a background process.
        $bgStatusFilename = $this->createBgProcStatus();
        $pid = $this->fork();

        // Parent process.
        if ($pid > 0) {
            $this->initializeBgProcStatus($bgStatusFilename, $pid);
            return $bgStatusFilename;
        }

        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            $cmd = sprintf(
                "omv-mkconf %s install 2>&1",
                "deluge"
            );

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            exit(0);
        } catch (Exception $e) {
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            exit(1);
        }
        // Send a modify signal so that the user can trigger a restart.
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath);
    }

    public function doOld($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Create a background process.
        $bgStatusFilename = $this->createBgProcStatus();
        $pid = $this->fork();

        // Parent process.
        if ($pid > 0) {
            $this->initializeBgProcStatus($bgStatusFilename, $pid);
            return $bgStatusFilename;
        }

        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            $cmd = sprintf(
                "omv-mkconf %s old 2>&1",
                "deluge"
            );

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            exit(0);
        } catch (Exception $e) {
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            exit(1);
        }
        // Send a modify signal so that the user can trigger a restart.
        $this->dispatcher->notify(OMV_NOTIFY_MODIFY, $this->eventMessagePath);
    }

    public function doReset($params, $context)
    {
        $this->validateMethodContext($context, ['role' => OMV_ROLE_ADMINISTRATOR]);

        // Create a background process.
        $bgStatusFilename = $this->createBgProcStatus();
        $pid = $this->fork();

        // Parent process.
        if ($pid > 0) {
            $this->initializeBgProcStatus($bgStatusFilename, $pid);
            return $bgStatusFilename;
        }

        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            $cmd = sprintf(
                "omv-mkconf %s reset 2>&1",
                "deluge"
            );

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            exit(0);
        } catch (Exception $e) {
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            exit(1);
        }
    }
}
